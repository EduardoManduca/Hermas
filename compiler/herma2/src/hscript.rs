use std::collections::{BTreeMap, BTreeSet};
use std::error::Error;
use std::fmt;

use crate::catalog::{ActionId, Catalog, Compensation, TypeId};
use crate::graph::{
    EachRegion, EdgeSource, EdgeTarget, GraphBuilder, MAX_ALL_BRANCHES, SourceLocation,
    TerminalKind, VerifiedGraph,
};
use crate::schema::Span;

const MAX_WORKFLOW_ACTIONS: usize = 64;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct HScriptDiagnostic {
    pub file: String,
    pub stage: &'static str,
    pub code: &'static str,
    pub span: Span,
    pub message: String,
}

impl HScriptDiagnostic {
    fn new(
        file: impl Into<String>,
        stage: &'static str,
        code: &'static str,
        span: Span,
        message: impl Into<String>,
    ) -> Self {
        Self {
            file: file.into(),
            stage,
            code,
            span,
            message: message.into(),
        }
    }
}

impl fmt::Display for HScriptDiagnostic {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "{}:{}:{}: {}/{}: {}",
            self.file, self.span.line, self.span.column, self.stage, self.code, self.message
        )
    }
}

impl Error for HScriptDiagnostic {}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Symbol {
    LeftBrace,
    RightBrace,
    LeftParen,
    RightParen,
    Colon,
    DoubleColon,
    Equals,
    Dot,
    Slash,
    Pipe,
    Arrow,
}

#[derive(Clone, Debug, Eq, PartialEq)]
enum TokenKind {
    Identifier(String),
    Duration(u64),
    Number(u64),
    Symbol(Symbol),
    End,
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct Token {
    kind: TokenKind,
    span: Span,
}

fn identifier_start(byte: u8) -> bool {
    byte.is_ascii_alphabetic() || byte == b'_'
}

fn identifier_continue(byte: u8) -> bool {
    identifier_start(byte) || byte.is_ascii_digit() || byte == b'-'
}

fn lex(file: &str, source: &str) -> Result<Vec<Token>, HScriptDiagnostic> {
    let bytes = source.as_bytes();
    let mut tokens = Vec::new();
    let mut offset = 0usize;
    let mut line = 1usize;
    let mut column = 1usize;
    while offset < bytes.len() {
        let byte = bytes[offset];
        if matches!(byte, b' ' | b'\t' | b'\r' | b',' | b';') {
            offset += 1;
            column += 1;
            continue;
        }
        if byte == b'\n' {
            offset += 1;
            line += 1;
            column = 1;
            continue;
        }
        if byte == b'#' {
            while offset < bytes.len() && bytes[offset] != b'\n' {
                offset += 1;
                column += 1;
            }
            continue;
        }
        let start = Span {
            offset,
            length: 1,
            line,
            column,
        };
        if identifier_start(byte) {
            let begin = offset;
            while offset < bytes.len() && identifier_continue(bytes[offset]) {
                offset += 1;
                column += 1;
            }
            tokens.push(Token {
                kind: TokenKind::Identifier(source[begin..offset].to_owned()),
                span: Span {
                    length: offset - begin,
                    ..start
                },
            });
            continue;
        }
        if byte.is_ascii_digit() {
            let begin = offset;
            let mut value = 0u64;
            while offset < bytes.len() && bytes[offset].is_ascii_digit() {
                value = value
                    .checked_mul(10)
                    .and_then(|current| current.checked_add(u64::from(bytes[offset] - b'0')))
                    .ok_or_else(|| {
                        HScriptDiagnostic::new(
                            file,
                            "syntax",
                            "duration-overflow",
                            start,
                            "deadline duration exceeds u64",
                        )
                    })?;
                offset += 1;
                column += 1;
            }
            let multiplier = if source[offset..].starts_with("ms") {
                offset += 2;
                column += 2;
                Some(1u64)
            } else if source[offset..].starts_with('s') {
                offset += 1;
                column += 1;
                Some(1000u64)
            } else {
                None
            };
            let kind = if let Some(multiplier) = multiplier {
                TokenKind::Duration(value.checked_mul(multiplier).ok_or_else(|| {
                    HScriptDiagnostic::new(
                        file,
                        "syntax",
                        "duration-overflow",
                        start,
                        "deadline duration exceeds u64 milliseconds",
                    )
                })?)
            } else {
                TokenKind::Number(value)
            };
            tokens.push(Token {
                kind,
                span: Span {
                    length: offset - begin,
                    ..start
                },
            });
            continue;
        }
        let (symbol, length) = match byte {
            b'{' => (Symbol::LeftBrace, 1),
            b'}' => (Symbol::RightBrace, 1),
            b'(' => (Symbol::LeftParen, 1),
            b')' => (Symbol::RightParen, 1),
            b':' if bytes.get(offset + 1) == Some(&b':') => (Symbol::DoubleColon, 2),
            b':' => (Symbol::Colon, 1),
            b'=' => (Symbol::Equals, 1),
            b'.' => (Symbol::Dot, 1),
            b'/' => (Symbol::Slash, 1),
            b'|' if bytes.get(offset + 1) == Some(&b'>') => (Symbol::Pipe, 2),
            b'-' if bytes.get(offset + 1) == Some(&b'>') => (Symbol::Arrow, 2),
            _ => {
                return Err(HScriptDiagnostic::new(
                    file,
                    "syntax",
                    "unexpected-byte",
                    start,
                    format!("unexpected byte 0x{byte:02x}"),
                ));
            }
        };
        tokens.push(Token {
            kind: TokenKind::Symbol(symbol),
            span: Span { length, ..start },
        });
        offset += length;
        column += length;
    }
    tokens.push(Token {
        kind: TokenKind::End,
        span: Span {
            offset,
            length: 0,
            line,
            column,
        },
    });
    Ok(tokens)
}

struct Tokens {
    file: String,
    items: Vec<Token>,
    index: usize,
}

impl Tokens {
    fn from_source(file: &str, source: &str) -> Result<Self, HScriptDiagnostic> {
        Ok(Self {
            file: file.to_owned(),
            items: lex(file, source)?,
            index: 0,
        })
    }

    fn peek(&self) -> &Token {
        &self.items[self.index]
    }

    fn lookahead(&self, distance: usize) -> &Token {
        self.items
            .get(self.index + distance)
            .unwrap_or_else(|| self.items.last().expect("token stream contains EOF"))
    }

    fn advance(&mut self) -> Token {
        let token = self.items[self.index].clone();
        if !matches!(token.kind, TokenKind::End) {
            self.index += 1;
        }
        token
    }

    fn check_keyword(&self, expected: &str) -> bool {
        matches!(&self.peek().kind, TokenKind::Identifier(value) if value == expected)
    }

    fn check_symbol(&self, expected: Symbol) -> bool {
        matches!(self.peek().kind, TokenKind::Symbol(value) if value == expected)
    }

    fn expect_keyword(&mut self, expected: &'static str) -> Result<Span, HScriptDiagnostic> {
        let token = self.advance();
        match token.kind {
            TokenKind::Identifier(value) if value == expected => Ok(token.span),
            _ => Err(self.unexpected(token.span, format!("expected `{expected}`"))),
        }
    }

    fn expect_identifier(&mut self) -> Result<(String, Span), HScriptDiagnostic> {
        let token = self.advance();
        match token.kind {
            TokenKind::Identifier(value) => Ok((value, token.span)),
            _ => Err(self.unexpected(token.span, "expected identifier")),
        }
    }

    fn expect_duration(&mut self) -> Result<(u64, Span), HScriptDiagnostic> {
        let token = self.advance();
        match token.kind {
            TokenKind::Duration(value) if value != 0 => Ok((value, token.span)),
            TokenKind::Number(_) => Err(HScriptDiagnostic::new(
                &self.file,
                "syntax",
                "invalid-duration",
                token.span,
                "deadline duration must use `ms` or `s`",
            )),
            _ => Err(self.unexpected(token.span, "expected a nonzero duration")),
        }
    }

    fn expect_number(&mut self) -> Result<(u64, Span), HScriptDiagnostic> {
        let token = self.advance();
        match token.kind {
            TokenKind::Number(value) => Ok((value, token.span)),
            _ => Err(self.unexpected(token.span, "expected integer")),
        }
    }

    fn expect_symbol(&mut self, expected: Symbol) -> Result<Span, HScriptDiagnostic> {
        let token = self.advance();
        match token.kind {
            TokenKind::Symbol(value) if value == expected => Ok(token.span),
            _ => Err(self.unexpected(token.span, format!("expected {expected:?}"))),
        }
    }

    fn unexpected(&self, span: Span, message: impl Into<String>) -> HScriptDiagnostic {
        HScriptDiagnostic::new(&self.file, "syntax", "unexpected-token", span, message)
    }
}

#[derive(Clone, Debug)]
struct TypeReference {
    app: String,
    name: String,
    span: Span,
}

#[derive(Clone, Debug)]
struct Parameter {
    name: String,
    type_ref: TypeReference,
    span: Span,
}

#[derive(Clone, Debug)]
struct Invocation {
    app: String,
    action: String,
    presentation: Option<TypeReference>,
    span: Span,
    connection_span: Span,
}

#[derive(Clone, Debug)]
enum PipelineSource {
    WorkflowInput,
    Binding(String, Span),
    ProductField(String, String, Span),
}

#[derive(Clone, Debug)]
struct Pipeline {
    source: PipelineSource,
    invocations: Vec<Invocation>,
}

#[derive(Clone, Debug)]
enum Statement {
    Let {
        name: String,
        name_span: Span,
        pipeline: Pipeline,
    },
    Return {
        span: Span,
        pipeline: Pipeline,
    },
    Match {
        span: Span,
        binding: String,
        binding_span: Span,
        cases: Vec<MatchCase>,
    },
    All {
        span: Span,
        name: String,
        name_span: Span,
        source: String,
        source_span: Span,
        branches: Vec<AllBranch>,
    },
    Each {
        span: Span,
        name: String,
        name_span: Span,
        item: String,
        item_span: Span,
        source: String,
        source_span: Span,
        concurrency: u8,
        pipeline: Box<Pipeline>,
        collected: TypeReference,
    },
    WithinReturn {
        duration_ms: u64,
        return_span: Span,
        pipeline: Pipeline,
    },
}

#[derive(Clone, Debug)]
struct MatchCase {
    name: String,
    name_span: Span,
    binding: String,
    binding_span: Span,
    pipeline: Pipeline,
}

#[derive(Clone, Debug)]
struct AllBranch {
    name: String,
    name_span: Span,
    pipeline: Pipeline,
}

#[derive(Clone, Debug)]
struct Workflow {
    name: String,
    name_span: Span,
    span: Span,
    parameter: Option<Parameter>,
    success: TypeReference,
    errors: Vec<TypeReference>,
    errors_span: Span,
    statements: Vec<Statement>,
    deadline_ms: Option<u64>,
    saga: bool,
}

struct HScriptModule {
    name: Option<String>,
    workflows: Vec<Workflow>,
}

fn joined_span(first: Span, last: Span) -> Span {
    Span {
        length: last
            .offset
            .saturating_add(last.length)
            .saturating_sub(first.offset),
        ..first
    }
}

fn parse_type_reference(tokens: &mut Tokens) -> Result<TypeReference, HScriptDiagnostic> {
    let (app, app_span) = tokens.expect_identifier()?;
    tokens.expect_symbol(Symbol::DoubleColon)?;
    let (name, name_span) = tokens.expect_identifier()?;
    Ok(TypeReference {
        app,
        name,
        span: joined_span(app_span, name_span),
    })
}

fn parse_invocation(
    tokens: &mut Tokens,
    presentation: Option<TypeReference>,
    connection_span: Span,
) -> Result<Invocation, HScriptDiagnostic> {
    let (app, app_span) = tokens.expect_identifier()?;
    tokens.expect_symbol(Symbol::Slash)?;
    let (action, _) = tokens.expect_identifier()?;
    tokens.expect_symbol(Symbol::LeftParen)?;
    let right = tokens.expect_symbol(Symbol::RightParen)?;
    Ok(Invocation {
        app,
        action,
        presentation,
        span: joined_span(app_span, right),
        connection_span,
    })
}

fn parse_pipeline(tokens: &mut Tokens) -> Result<Pipeline, HScriptDiagnostic> {
    let first_span = tokens.peek().span;
    let starts_with_invocation =
        matches!(tokens.lookahead(1).kind, TokenKind::Symbol(Symbol::Slash));
    let (source, mut invocations) = if starts_with_invocation {
        let mut invocation = parse_invocation(tokens, None, first_span)?;
        invocation.connection_span = invocation.span;
        (PipelineSource::WorkflowInput, vec![invocation])
    } else {
        let (binding, span) = tokens.expect_identifier()?;
        if tokens.check_symbol(Symbol::Dot) {
            tokens.expect_symbol(Symbol::Dot)?;
            let (field, field_span) = tokens.expect_identifier()?;
            (
                PipelineSource::ProductField(binding, field, joined_span(span, field_span)),
                Vec::new(),
            )
        } else {
            (PipelineSource::Binding(binding, span), Vec::new())
        }
    };
    while tokens.check_symbol(Symbol::Pipe) {
        let pipe_span = tokens.expect_symbol(Symbol::Pipe)?;
        let presentation = if tokens.check_keyword("as") {
            tokens.expect_keyword("as")?;
            let presentation = parse_type_reference(tokens)?;
            tokens.expect_symbol(Symbol::Pipe)?;
            Some(presentation)
        } else {
            None
        };
        invocations.push(parse_invocation(tokens, presentation, pipe_span)?);
    }
    if invocations.is_empty() {
        return Err(tokens.unexpected(
            tokens.peek().span,
            "a pipeline must invoke at least one Action",
        ));
    }
    Ok(Pipeline {
        source,
        invocations,
    })
}

fn parse_workflow(file: &str, tokens: &mut Tokens) -> Result<Workflow, HScriptDiagnostic> {
    let workflow_span = tokens.expect_keyword("workflow")?;
    let (name, name_span) = tokens.expect_identifier()?;
    tokens.expect_symbol(Symbol::LeftParen)?;
    let parameter = if tokens.check_symbol(Symbol::RightParen) {
        None
    } else {
        let (parameter_name, parameter_span) = tokens.expect_identifier()?;
        tokens.expect_symbol(Symbol::Colon)?;
        Some(Parameter {
            name: parameter_name,
            type_ref: parse_type_reference(tokens)?,
            span: parameter_span,
        })
    };
    tokens.expect_symbol(Symbol::RightParen)?;
    tokens.expect_symbol(Symbol::Arrow)?;
    let success = parse_type_reference(tokens)?;
    let errors_span = tokens.expect_keyword("errors")?;
    tokens.expect_symbol(Symbol::LeftBrace)?;
    let mut errors = Vec::new();
    while !tokens.check_symbol(Symbol::RightBrace) {
        if matches!(tokens.peek().kind, TokenKind::End) {
            return Err(tokens.unexpected(tokens.peek().span, "expected workflow error type"));
        }
        errors.push(parse_type_reference(tokens)?);
    }
    tokens.expect_symbol(Symbol::RightBrace)?;
    if errors.is_empty() {
        return Err(HScriptDiagnostic::new(
            file,
            "workflow",
            "missing-error",
            errors_span,
            "a workflow must declare at least one known error type",
        ));
    }
    tokens.expect_symbol(Symbol::LeftBrace)?;
    let saga = if tokens.check_keyword("saga") {
        tokens.expect_keyword("saga")?;
        tokens.expect_symbol(Symbol::LeftBrace)?;
        true
    } else {
        false
    };
    let deadline_ms = if tokens.check_keyword("within") {
        tokens.expect_keyword("within")?;
        let (duration, _) = tokens.expect_duration()?;
        tokens.expect_symbol(Symbol::LeftBrace)?;
        Some(duration)
    } else {
        None
    };
    let mut statements = Vec::new();
    let mut returned = false;
    while !tokens.check_symbol(Symbol::RightBrace) {
        if matches!(tokens.peek().kind, TokenKind::End) {
            return Err(tokens.unexpected(tokens.peek().span, "expected workflow body"));
        }
        if returned {
            return Err(HScriptDiagnostic::new(
                file,
                "workflow",
                "after-return",
                tokens.peek().span,
                "no statement may follow `return`",
            ));
        }
        if tokens.check_keyword("let") {
            tokens.expect_keyword("let")?;
            let (binding, binding_span) = tokens.expect_identifier()?;
            tokens.expect_symbol(Symbol::Equals)?;
            if tokens.check_keyword("all") {
                let span = tokens.expect_keyword("all")?;
                let (source, source_span) = tokens.expect_identifier()?;
                tokens.expect_symbol(Symbol::LeftBrace)?;
                let mut branches = Vec::new();
                while !tokens.check_symbol(Symbol::RightBrace) {
                    let (name, name_span) = tokens.expect_identifier()?;
                    tokens.expect_symbol(Symbol::Equals)?;
                    branches.push(AllBranch {
                        name,
                        name_span,
                        pipeline: parse_pipeline(tokens)?,
                    });
                }
                tokens.expect_symbol(Symbol::RightBrace)?;
                statements.push(Statement::All {
                    span,
                    name: binding,
                    name_span: binding_span,
                    source,
                    source_span,
                    branches,
                });
            } else if tokens.check_keyword("each") {
                let span = tokens.expect_keyword("each")?;
                let (item, item_span) = tokens.expect_identifier()?;
                tokens.expect_keyword("in")?;
                let (source, source_span) = tokens.expect_identifier()?;
                tokens.expect_keyword("concurrency")?;
                let (concurrency, concurrency_span) = tokens.expect_number()?;
                let concurrency = u8::try_from(concurrency).map_err(|_| {
                    HScriptDiagnostic::new(
                        file,
                        "syntax",
                        "invalid-concurrency",
                        concurrency_span,
                        "Each concurrency must fit in u8",
                    )
                })?;
                tokens.expect_symbol(Symbol::LeftBrace)?;
                let pipeline = parse_pipeline(tokens)?;
                tokens.expect_symbol(Symbol::RightBrace)?;
                tokens.expect_keyword("collect")?;
                tokens.expect_keyword("as")?;
                let collected = parse_type_reference(tokens)?;
                statements.push(Statement::Each {
                    span,
                    name: binding,
                    name_span: binding_span,
                    item,
                    item_span,
                    source,
                    source_span,
                    concurrency,
                    pipeline: Box::new(pipeline),
                    collected,
                });
            } else {
                statements.push(Statement::Let {
                    name: binding,
                    name_span: binding_span,
                    pipeline: parse_pipeline(tokens)?,
                });
            }
        } else if tokens.check_keyword("match") {
            let span = tokens.expect_keyword("match")?;
            let (binding, binding_span) = tokens.expect_identifier()?;
            tokens.expect_symbol(Symbol::LeftBrace)?;
            let mut cases = Vec::new();
            while !tokens.check_symbol(Symbol::RightBrace) {
                tokens.expect_keyword("case")?;
                let (name, name_span) = tokens.expect_identifier()?;
                let (case_binding, case_binding_span) = tokens.expect_identifier()?;
                tokens.expect_symbol(Symbol::LeftBrace)?;
                if tokens.check_keyword("return") {
                    tokens.expect_keyword("return")?;
                }
                let pipeline = parse_pipeline(tokens)?;
                tokens.expect_symbol(Symbol::RightBrace)?;
                cases.push(MatchCase {
                    name,
                    name_span,
                    binding: case_binding,
                    binding_span: case_binding_span,
                    pipeline,
                });
            }
            tokens.expect_symbol(Symbol::RightBrace)?;
            if cases.is_empty() {
                return Err(HScriptDiagnostic::new(
                    file,
                    "syntax",
                    "empty-match",
                    span,
                    "a match must declare at least one case",
                ));
            }
            statements.push(Statement::Match {
                span,
                binding,
                binding_span,
                cases,
            });
            returned = true;
        } else if tokens.check_keyword("within") {
            tokens.expect_keyword("within")?;
            let (duration_ms, _) = tokens.expect_duration()?;
            tokens.expect_symbol(Symbol::LeftBrace)?;
            let return_span = tokens.expect_keyword("return")?;
            let pipeline = parse_pipeline(tokens)?;
            tokens.expect_symbol(Symbol::RightBrace)?;
            statements.push(Statement::WithinReturn {
                duration_ms,
                return_span,
                pipeline,
            });
            returned = true;
        } else if tokens.check_keyword("return") {
            let span = tokens.expect_keyword("return")?;
            statements.push(Statement::Return {
                span,
                pipeline: parse_pipeline(tokens)?,
            });
            returned = true;
        } else {
            let span = tokens.peek().span;
            statements.push(Statement::Return {
                span,
                pipeline: parse_pipeline(tokens)?,
            });
            returned = true;
        }
    }
    tokens.expect_symbol(Symbol::RightBrace)?;
    if deadline_ms.is_some() {
        tokens.expect_symbol(Symbol::RightBrace)?;
    }
    if saga {
        tokens.expect_symbol(Symbol::RightBrace)?;
    }
    if statements.is_empty() || !returned {
        return Err(HScriptDiagnostic::new(
            file,
            "workflow",
            "missing-return",
            workflow_span,
            "workflow body must end with a returned pipeline",
        ));
    }
    Ok(Workflow {
        name,
        name_span,
        span: workflow_span,
        parameter,
        success,
        errors,
        errors_span,
        statements,
        deadline_ms,
        saga,
    })
}

fn parse_module(file: &str, source: &str) -> Result<HScriptModule, HScriptDiagnostic> {
    let mut tokens = Tokens::from_source(file, source)?;
    let name = if tokens.check_keyword("module") {
        tokens.expect_keyword("module")?;
        Some(tokens.expect_identifier()?.0)
    } else {
        None
    };
    let mut workflows = Vec::new();
    let mut names = BTreeMap::new();
    while !matches!(tokens.peek().kind, TokenKind::End) {
        let workflow = parse_workflow(file, &mut tokens)?;
        if names
            .insert(workflow.name.clone(), workflow.name_span)
            .is_some()
        {
            return Err(HScriptDiagnostic::new(
                file,
                "module",
                "duplicate-workflow",
                workflow.name_span,
                format!(
                    "workflow `{}` is declared more than once in this module",
                    workflow.name
                ),
            ));
        }
        workflows.push(workflow);
    }
    if workflows.is_empty() {
        return Err(HScriptDiagnostic::new(
            file,
            "module",
            "missing-workflow",
            tokens.peek().span,
            "an HScript2 module must declare at least one workflow",
        ));
    }
    if workflows.len() > 1 && name.is_none() {
        return Err(HScriptDiagnostic::new(
            file,
            "module",
            "missing-module-name",
            workflows[1].name_span,
            "a module with multiple workflows must declare a stable module name",
        ));
    }
    Ok(HScriptModule { name, workflows })
}

fn qualified_workflow_name(module: &HScriptModule, workflow: &Workflow) -> String {
    module.name.as_ref().map_or_else(
        || workflow.name.clone(),
        |name| format!("{name}::{}", workflow.name),
    )
}

#[derive(Clone, Copy, Debug)]
enum ValueOrigin {
    WorkflowInput,
    ActionSuccess(usize),
    DispatchCase(usize, u8),
    ForkBranch(usize, u8),
    JoinField(usize, u8),
    EachItem(usize),
    EachOutput(usize),
}

#[derive(Clone, Debug)]
struct Binding {
    origin: ValueOrigin,
    type_id: TypeId,
    consumed: bool,
    span: Span,
}

#[derive(Clone, Debug)]
struct CompiledStep {
    action: ActionId,
    input: ValueOrigin,
    presentation: Option<TypeId>,
    invocation_span: Span,
    connection_span: Span,
}

#[derive(Clone, Debug)]
struct CompiledDispatch {
    input: ValueOrigin,
    variant: TypeId,
    span: Span,
}

#[derive(Clone, Debug)]
struct CompiledAll {
    input: ValueOrigin,
    input_type: TypeId,
    branch_outputs: Vec<(ValueOrigin, TypeId)>,
    span: Span,
}

#[derive(Clone, Debug)]
struct CompiledDeadline {
    duration_ms: u64,
    first_step: usize,
    step_count: usize,
}

#[derive(Clone, Debug)]
struct CompiledEach {
    input: ValueOrigin,
    source_list: TypeId,
    item_input: TypeId,
    item_output: TypeId,
    collected: TypeId,
    bound: u16,
    concurrency: u8,
    template_step: usize,
    span: Span,
}

#[derive(Clone, Debug)]
struct ProductField {
    origin: ValueOrigin,
    type_id: TypeId,
    consumed: bool,
}

#[derive(Clone, Debug)]
struct ProductBinding {
    fields: BTreeMap<String, ProductField>,
    span: Span,
}

struct LoweringContext<'a> {
    catalog: &'a Catalog,
    file: &'a str,
    bindings: BTreeMap<String, Binding>,
    products: BTreeMap<String, ProductBinding>,
    input_type: Option<TypeId>,
    input_consumed: bool,
    workflow_errors: &'a BTreeSet<TypeId>,
    steps: Vec<CompiledStep>,
    dispatches: Vec<CompiledDispatch>,
    all_scopes: Vec<CompiledAll>,
    deadlines: Vec<CompiledDeadline>,
    each_regions: Vec<CompiledEach>,
}

fn source_location(file: &str, span: Span) -> SourceLocation {
    SourceLocation {
        file: file.to_owned(),
        offset: span.offset,
        length: span.length,
        line: span.line,
        column: span.column,
    }
}

fn resolve_type(
    catalog: &Catalog,
    file: &str,
    type_ref: &TypeReference,
) -> Result<TypeId, HScriptDiagnostic> {
    catalog
        .resolve_type(&type_ref.app, &type_ref.name)
        .ok_or_else(|| {
            HScriptDiagnostic::new(
                file,
                "resolution",
                "unknown-type",
                type_ref.span,
                format!("unknown type `{}::{}`", type_ref.app, type_ref.name),
            )
        })
}

fn take_source(
    file: &str,
    pipeline: &Pipeline,
    bindings: &mut BTreeMap<String, Binding>,
    products: &mut BTreeMap<String, ProductBinding>,
    input_type: Option<TypeId>,
    input_consumed: &mut bool,
) -> Result<(ValueOrigin, Option<TypeId>), HScriptDiagnostic> {
    match &pipeline.source {
        PipelineSource::WorkflowInput => {
            if *input_consumed {
                return Err(HScriptDiagnostic::new(
                    file,
                    "workflow",
                    "input-already-consumed",
                    pipeline.invocations[0].span,
                    "the sequential workflow input has already been consumed",
                ));
            }
            *input_consumed = true;
            Ok((ValueOrigin::WorkflowInput, input_type))
        }
        PipelineSource::Binding(name, span) => {
            let binding = bindings.get_mut(name).ok_or_else(|| {
                HScriptDiagnostic::new(
                    file,
                    "workflow",
                    "unknown-binding",
                    *span,
                    format!("unknown immutable binding `{name}`"),
                )
            })?;
            if binding.consumed {
                return Err(HScriptDiagnostic::new(
                    file,
                    "workflow",
                    "binding-already-consumed",
                    *span,
                    format!("binding `{name}` was already consumed by the sequential graph"),
                ));
            }
            binding.consumed = true;
            if matches!(binding.origin, ValueOrigin::WorkflowInput) {
                if *input_consumed {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "workflow",
                        "input-already-consumed",
                        *span,
                        "the sequential workflow input has already been consumed",
                    ));
                }
                *input_consumed = true;
            }
            Ok((binding.origin, Some(binding.type_id)))
        }
        PipelineSource::ProductField(product, field, span) => {
            let product_binding = products.get_mut(product).ok_or_else(|| {
                HScriptDiagnostic::new(
                    file,
                    "workflow",
                    "unknown-product",
                    *span,
                    format!("unknown `all` result `{product}`"),
                )
            })?;
            let field_binding = product_binding.fields.get_mut(field).ok_or_else(|| {
                HScriptDiagnostic::new(
                    file,
                    "workflow",
                    "unknown-product-field",
                    *span,
                    format!("`{product}` has no result field `{field}`"),
                )
            })?;
            if field_binding.consumed {
                return Err(HScriptDiagnostic::new(
                    file,
                    "workflow",
                    "product-field-already-consumed",
                    *span,
                    format!("`{product}.{field}` was already consumed"),
                ));
            }
            field_binding.consumed = true;
            Ok((field_binding.origin, Some(field_binding.type_id)))
        }
    }
}

fn lower_pipeline(
    context: &mut LoweringContext<'_>,
    pipeline: &Pipeline,
) -> Result<(ValueOrigin, TypeId), HScriptDiagnostic> {
    let (mut origin, mut value_type) = take_source(
        context.file,
        pipeline,
        &mut context.bindings,
        &mut context.products,
        context.input_type,
        &mut context.input_consumed,
    )?;
    lower_pipeline_tail(context, pipeline, &mut origin, &mut value_type)
}

fn lower_pipeline_from(
    context: &mut LoweringContext<'_>,
    pipeline: &Pipeline,
    origin: ValueOrigin,
    value_type: TypeId,
) -> Result<(ValueOrigin, TypeId), HScriptDiagnostic> {
    let mut origin = origin;
    let mut value_type = Some(value_type);
    lower_pipeline_tail(context, pipeline, &mut origin, &mut value_type)
}

fn lower_pipeline_tail(
    context: &mut LoweringContext<'_>,
    pipeline: &Pipeline,
    origin: &mut ValueOrigin,
    value_type: &mut Option<TypeId>,
) -> Result<(ValueOrigin, TypeId), HScriptDiagnostic> {
    for invocation in &pipeline.invocations {
        if context.steps.len() >= MAX_WORKFLOW_ACTIONS {
            return Err(HScriptDiagnostic::new(
                context.file,
                "workflow",
                "limit-exceeded",
                invocation.span,
                format!("a workflow may invoke at most {MAX_WORKFLOW_ACTIONS} Actions"),
            ));
        }
        let action_id = context
            .catalog
            .resolve_action(&invocation.app, &invocation.action)
            .ok_or_else(|| {
                HScriptDiagnostic::new(
                    context.file,
                    "resolution",
                    "unknown-action",
                    invocation.span,
                    format!("unknown Action `{}/{}`", invocation.app, invocation.action),
                )
            })?;
        let action = context
            .catalog
            .action(action_id)
            .expect("resolved Action exists");
        let source_type = value_type.unwrap_or(action.input);
        let presentation = invocation
            .presentation
            .as_ref()
            .map(|type_ref| resolve_type(context.catalog, context.file, type_ref))
            .transpose()?;
        if let Some(presentation_type) = presentation {
            if presentation_type != action.input {
                return Err(HScriptDiagnostic::new(
                    context.file,
                    "typing",
                    "wrong-presentation-target",
                    invocation
                        .presentation
                        .as_ref()
                        .expect("presentation exists")
                        .span,
                    format!(
                        "presentation names {}, but `{}/{}` requires {}",
                        context.catalog.type_name(presentation_type),
                        invocation.app,
                        invocation.action,
                        context.catalog.type_name(action.input)
                    ),
                ));
            }
            context
                .catalog
                .representation_compatible(source_type, presentation_type)
                .map_err(|reason| {
                    HScriptDiagnostic::new(
                        context.file,
                        "typing",
                        "incompatible-presentation",
                        invocation
                            .presentation
                            .as_ref()
                            .expect("presentation exists")
                            .span,
                        reason,
                    )
                })?;
        } else if source_type != action.input {
            return Err(HScriptDiagnostic::new(
                context.file,
                "typing",
                "nominal-mismatch",
                invocation.span,
                format!(
                    "{} cannot flow directly into {}; add an explicit `as {}` presentation if their representations are compatible",
                    context.catalog.type_name(source_type),
                    context.catalog.action_name(action_id),
                    context.catalog.type_name(action.input)
                ),
            ));
        }
        if !context.workflow_errors.contains(&action.error) {
            return Err(HScriptDiagnostic::new(
                context.file,
                "workflow",
                "unhandled-action-error",
                invocation.span,
                format!(
                    "{} returns {}, which is not declared by the workflow",
                    context.catalog.action_name(action_id),
                    context.catalog.type_name(action.error)
                ),
            ));
        }
        let step_index = context.steps.len();
        context.steps.push(CompiledStep {
            action: action_id,
            input: *origin,
            presentation,
            invocation_span: invocation.span,
            connection_span: invocation.connection_span,
        });
        *origin = ValueOrigin::ActionSuccess(step_index);
        *value_type = Some(action.success);
    }
    Ok((
        *origin,
        value_type.expect("pipeline invokes at least one Action"),
    ))
}

fn compile_workflow(
    catalog: &Catalog,
    file: &str,
    workflow: &Workflow,
    graph_name: &str,
) -> Result<VerifiedGraph, HScriptDiagnostic> {
    let success_type = resolve_type(catalog, file, &workflow.success)?;
    let mut workflow_errors = BTreeSet::new();
    let mut error_types = Vec::new();
    for error in &workflow.errors {
        let type_id = resolve_type(catalog, file, error)?;
        if !workflow_errors.insert(type_id) {
            return Err(HScriptDiagnostic::new(
                file,
                "workflow",
                "duplicate-error",
                error.span,
                format!(
                    "workflow error `{}` is declared twice",
                    catalog.type_name(type_id)
                ),
            ));
        }
        error_types.push(type_id);
    }
    let explicit_input = workflow
        .parameter
        .as_ref()
        .map(|parameter| resolve_type(catalog, file, &parameter.type_ref))
        .transpose()?;
    let mut initial_bindings = BTreeMap::new();
    if let Some(parameter) = &workflow.parameter {
        initial_bindings.insert(
            parameter.name.clone(),
            Binding {
                origin: ValueOrigin::WorkflowInput,
                type_id: explicit_input.expect("resolved explicit input"),
                consumed: false,
                span: parameter.span,
            },
        );
    }
    let mut inferred_input = explicit_input;
    let mut context = LoweringContext {
        catalog,
        file,
        bindings: initial_bindings,
        products: BTreeMap::new(),
        input_type: inferred_input,
        input_consumed: false,
        workflow_errors: &workflow_errors,
        steps: Vec::new(),
        dispatches: Vec::new(),
        all_scopes: Vec::new(),
        deadlines: Vec::new(),
        each_regions: Vec::new(),
    };
    let mut returned = Vec::new();
    for statement in &workflow.statements {
        match statement {
            Statement::Let {
                name,
                name_span,
                pipeline,
            } => {
                if context.bindings.contains_key(name) {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "workflow",
                        "duplicate-binding",
                        *name_span,
                        format!("immutable binding `{name}` is already declared"),
                    ));
                }
                if inferred_input.is_none()
                    && matches!(pipeline.source, PipelineSource::WorkflowInput)
                {
                    let first = &pipeline.invocations[0];
                    let action = catalog
                        .resolve_action(&first.app, &first.action)
                        .and_then(|id| catalog.action(id))
                        .ok_or_else(|| {
                            HScriptDiagnostic::new(
                                file,
                                "resolution",
                                "unknown-action",
                                first.span,
                                format!("unknown Action `{}/{}`", first.app, first.action),
                            )
                        })?;
                    inferred_input = Some(action.input);
                    context.input_type = inferred_input;
                }
                let (origin, type_id) = lower_pipeline(&mut context, pipeline)?;
                context.bindings.insert(
                    name.clone(),
                    Binding {
                        origin,
                        type_id,
                        consumed: false,
                        span: *name_span,
                    },
                );
            }
            Statement::Return { span, pipeline } => {
                if inferred_input.is_none()
                    && matches!(pipeline.source, PipelineSource::WorkflowInput)
                {
                    let first = &pipeline.invocations[0];
                    let action = catalog
                        .resolve_action(&first.app, &first.action)
                        .and_then(|id| catalog.action(id))
                        .ok_or_else(|| {
                            HScriptDiagnostic::new(
                                file,
                                "resolution",
                                "unknown-action",
                                first.span,
                                format!("unknown Action `{}/{}`", first.app, first.action),
                            )
                        })?;
                    inferred_input = Some(action.input);
                    context.input_type = inferred_input;
                }
                let (origin, type_id) = lower_pipeline(&mut context, pipeline)?;
                if type_id != success_type {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "typing",
                        "wrong-success-type",
                        *span,
                        format!(
                            "returned pipeline produces {}, but workflow requires {}",
                            catalog.type_name(type_id),
                            catalog.type_name(success_type)
                        ),
                    ));
                }
                returned.push((origin, *span));
            }
            Statement::WithinReturn {
                duration_ms,
                return_span,
                pipeline,
            } => {
                if inferred_input.is_none()
                    && matches!(pipeline.source, PipelineSource::WorkflowInput)
                {
                    let first = &pipeline.invocations[0];
                    let action = catalog
                        .resolve_action(&first.app, &first.action)
                        .and_then(|id| catalog.action(id))
                        .ok_or_else(|| {
                            HScriptDiagnostic::new(
                                file,
                                "resolution",
                                "unknown-action",
                                first.span,
                                format!("unknown Action `{}/{}`", first.app, first.action),
                            )
                        })?;
                    inferred_input = Some(action.input);
                    context.input_type = inferred_input;
                }
                let first_step = context.steps.len();
                let (origin, type_id) = lower_pipeline(&mut context, pipeline)?;
                if type_id != success_type {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "typing",
                        "wrong-success-type",
                        *return_span,
                        format!(
                            "returned pipeline produces {}, but workflow requires {}",
                            catalog.type_name(type_id),
                            catalog.type_name(success_type)
                        ),
                    ));
                }
                context.deadlines.push(CompiledDeadline {
                    duration_ms: *duration_ms,
                    first_step,
                    step_count: context.steps.len() - first_step,
                });
                returned.push((origin, *return_span));
            }
            Statement::All {
                span,
                name,
                name_span,
                source,
                source_span,
                branches,
            } => {
                if context.bindings.contains_key(name) || context.products.contains_key(name) {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "workflow",
                        "duplicate-binding",
                        *name_span,
                        format!("immutable binding `{name}` is already declared"),
                    ));
                }
                if !(2..=MAX_ALL_BRANCHES).contains(&branches.len()) {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "workflow",
                        "all-branch-count",
                        *span,
                        format!("`all` requires 2..={MAX_ALL_BRANCHES} statically known branches"),
                    ));
                }
                let source_pipeline = Pipeline {
                    source: PipelineSource::Binding(source.clone(), *source_span),
                    invocations: Vec::new(),
                };
                let (shared_origin, shared_type) = take_source(
                    file,
                    &source_pipeline,
                    &mut context.bindings,
                    &mut context.products,
                    context.input_type,
                    &mut context.input_consumed,
                )?;
                let shared_type = shared_type.expect("named all source has a nominal type");
                let all_index = context.all_scopes.len();
                context.all_scopes.push(CompiledAll {
                    input: shared_origin,
                    input_type: shared_type,
                    branch_outputs: Vec::new(),
                    span: *span,
                });
                let mut fields = BTreeMap::new();
                let mut outputs = Vec::new();
                for (branch_index, branch) in branches.iter().enumerate() {
                    if !matches!(
                        &branch.pipeline.source,
                        PipelineSource::Binding(branch_source, _) if branch_source == source
                    ) {
                        return Err(HScriptDiagnostic::new(
                            file,
                            "workflow",
                            "invalid-all-branch",
                            branch.name_span,
                            format!(
                                "`all` branch `{}` must start from shared input `{source}`",
                                branch.name
                            ),
                        ));
                    }
                    if fields.contains_key(&branch.name) {
                        return Err(HScriptDiagnostic::new(
                            file,
                            "workflow",
                            "duplicate-all-field",
                            branch.name_span,
                            format!("`all` result field `{}` is declared twice", branch.name),
                        ));
                    }
                    let tag = u8::try_from(branch_index).expect("all branch bound fits u8");
                    let (origin, type_id) = lower_pipeline_from(
                        &mut context,
                        &branch.pipeline,
                        ValueOrigin::ForkBranch(all_index, tag),
                        shared_type,
                    )?;
                    outputs.push((origin, type_id));
                    fields.insert(
                        branch.name.clone(),
                        ProductField {
                            origin: ValueOrigin::JoinField(all_index, tag),
                            type_id,
                            consumed: false,
                        },
                    );
                }
                context.all_scopes[all_index].branch_outputs = outputs;
                context.products.insert(
                    name.clone(),
                    ProductBinding {
                        fields,
                        span: *name_span,
                    },
                );
            }
            Statement::Each {
                span,
                name,
                name_span,
                item,
                item_span,
                source,
                source_span,
                concurrency,
                pipeline,
                collected,
            } => {
                if context.bindings.contains_key(name) || context.products.contains_key(name) {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "workflow",
                        "duplicate-binding",
                        *name_span,
                        format!("immutable binding `{name}` is already declared"),
                    ));
                }
                if context.bindings.contains_key(item) || context.products.contains_key(item) {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "workflow",
                        "duplicate-binding",
                        *item_span,
                        format!("Each item binding `{item}` conflicts with an existing binding"),
                    ));
                }
                if !matches!(
                    &pipeline.source,
                    PipelineSource::Binding(pipeline_source, _) if pipeline_source == item
                ) {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "workflow",
                        "invalid-each-body",
                        *item_span,
                        "Each body must start from its item binding",
                    ));
                }
                let source_pipeline = Pipeline {
                    source: PipelineSource::Binding(source.clone(), *source_span),
                    invocations: Vec::new(),
                };
                let (input, source_list) = take_source(
                    file,
                    &source_pipeline,
                    &mut context.bindings,
                    &mut context.products,
                    context.input_type,
                    &mut context.input_consumed,
                )?;
                let source_list = source_list.expect("named Each source has a nominal type");
                let (item_input, bound) = catalog.list_element(source_list).ok_or_else(|| {
                    HScriptDiagnostic::new(
                        file,
                        "typing",
                        "each-source-not-list",
                        *source_span,
                        format!(
                            "{} is not a named bounded list",
                            catalog.type_name(source_list)
                        ),
                    )
                })?;
                if *concurrency == 0 || *concurrency > 8 || u16::from(*concurrency) > bound {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "workflow",
                        "invalid-each-concurrency",
                        *span,
                        format!(
                            "Each concurrency must be 1..={} for source bound {bound}",
                            bound.min(8)
                        ),
                    ));
                }
                let each_index = context.each_regions.len();
                let first_step = context.steps.len();
                let (output, item_output) = lower_pipeline_from(
                    &mut context,
                    pipeline,
                    ValueOrigin::EachItem(each_index),
                    item_input,
                )?;
                if context.steps.len() != first_step + 1
                    || !matches!(output, ValueOrigin::ActionSuccess(_))
                {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "workflow",
                        "each-template-shape",
                        *span,
                        "the initial Each template must contain exactly one Action",
                    ));
                }
                let collected_type = resolve_type(catalog, file, collected)?;
                let (collected_item, collected_bound) =
                    catalog.list_element(collected_type).ok_or_else(|| {
                        HScriptDiagnostic::new(
                            file,
                            "typing",
                            "collect-target-not-list",
                            collected.span,
                            "collect target must be a named bounded list",
                        )
                    })?;
                if collected_item != item_output || collected_bound < bound {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "typing",
                        "incompatible-collect",
                        collected.span,
                        format!(
                            "{} cannot collect {} items of {}",
                            catalog.type_name(collected_type),
                            bound,
                            catalog.type_name(item_output)
                        ),
                    ));
                }
                context.each_regions.push(CompiledEach {
                    input,
                    source_list,
                    item_input,
                    item_output,
                    collected: collected_type,
                    bound,
                    concurrency: *concurrency,
                    template_step: first_step,
                    span: *span,
                });
                context.bindings.insert(
                    name.clone(),
                    Binding {
                        origin: ValueOrigin::EachOutput(each_index),
                        type_id: collected_type,
                        consumed: false,
                        span: *name_span,
                    },
                );
            }
            Statement::Match {
                span,
                binding,
                binding_span,
                cases,
            } => {
                let matched = context.bindings.get_mut(binding).ok_or_else(|| {
                    HScriptDiagnostic::new(
                        file,
                        "workflow",
                        "unknown-binding",
                        *binding_span,
                        format!("unknown immutable binding `{binding}`"),
                    )
                })?;
                if matched.consumed {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "workflow",
                        "binding-already-consumed",
                        *binding_span,
                        format!("binding `{binding}` was already consumed"),
                    ));
                }
                matched.consumed = true;
                let matched_origin = matched.origin;
                let variant = matched.type_id;
                if matches!(matched_origin, ValueOrigin::WorkflowInput) {
                    if context.input_consumed {
                        return Err(HScriptDiagnostic::new(
                            file,
                            "workflow",
                            "input-already-consumed",
                            *binding_span,
                            "the workflow input has already been consumed",
                        ));
                    }
                    context.input_consumed = true;
                }
                let variant_cases = catalog.variant_cases(variant).ok_or_else(|| {
                    HScriptDiagnostic::new(
                        file,
                        "typing",
                        "match-non-variant",
                        *binding_span,
                        format!(
                            "{} is not a named typed variant",
                            catalog.type_name(variant)
                        ),
                    )
                })?;
                let mut declared_cases = BTreeMap::new();
                for case in cases {
                    if declared_cases.insert(case.name.clone(), case).is_some() {
                        return Err(HScriptDiagnostic::new(
                            file,
                            "typing",
                            "duplicate-match-case",
                            case.name_span,
                            format!("variant case `{}` is matched twice", case.name),
                        ));
                    }
                }
                let expected = variant_cases.keys().cloned().collect::<BTreeSet<_>>();
                let declared = declared_cases.keys().cloned().collect::<BTreeSet<_>>();
                if expected != declared {
                    let missing = expected.difference(&declared).cloned().collect::<Vec<_>>();
                    let extra = declared.difference(&expected).cloned().collect::<Vec<_>>();
                    return Err(HScriptDiagnostic::new(
                        file,
                        "typing",
                        "non-exhaustive-match",
                        *span,
                        format!("match cases differ; missing={missing:?} extra={extra:?}"),
                    ));
                }
                let dispatch_index = context.dispatches.len();
                context.dispatches.push(CompiledDispatch {
                    input: matched_origin,
                    variant,
                    span: *span,
                });
                for (tag, (case_name, payload_type)) in variant_cases.iter().enumerate() {
                    let case = declared_cases[case_name];
                    if context.bindings.contains_key(&case.binding) {
                        return Err(HScriptDiagnostic::new(
                            file,
                            "workflow",
                            "duplicate-binding",
                            case.binding_span,
                            format!(
                                "case binding `{}` conflicts with an existing binding",
                                case.binding
                            ),
                        ));
                    }
                    if !matches!(
                        &case.pipeline.source,
                        PipelineSource::Binding(name, _) if name == &case.binding
                    ) || case.pipeline.invocations.is_empty()
                    {
                        return Err(HScriptDiagnostic::new(
                            file,
                            "workflow",
                            "invalid-match-branch",
                            case.binding_span,
                            "a match branch must pipe its case binding into at least one Action",
                        ));
                    }
                    context.bindings.insert(
                        case.binding.clone(),
                        Binding {
                            origin: ValueOrigin::DispatchCase(
                                dispatch_index,
                                u8::try_from(tag).expect("graph case bound fits u8"),
                            ),
                            type_id: *payload_type,
                            consumed: false,
                            span: case.binding_span,
                        },
                    );
                    let (origin, type_id) = lower_pipeline(&mut context, &case.pipeline)?;
                    context.bindings.remove(&case.binding);
                    if type_id != success_type {
                        return Err(HScriptDiagnostic::new(
                            file,
                            "typing",
                            "wrong-success-type",
                            case.name_span,
                            format!(
                                "case `{case_name}` produces {}, but workflow requires {}",
                                catalog.type_name(type_id),
                                catalog.type_name(success_type)
                            ),
                        ));
                    }
                    returned.push((origin, case.name_span));
                }
            }
        }
    }
    for (name, binding) in &context.bindings {
        if !binding.consumed {
            return Err(HScriptDiagnostic::new(
                file,
                "workflow",
                "unused-binding",
                binding.span,
                format!("binding `{name}` is never consumed"),
            ));
        }
    }
    for (name, product) in &context.products {
        if product.fields.values().all(|field| !field.consumed) {
            return Err(HScriptDiagnostic::new(
                file,
                "workflow",
                "unused-binding",
                product.span,
                format!("`all` result `{name}` is never consumed"),
            ));
        }
    }
    let input_type = inferred_input.expect("a returned pipeline invokes at least one Action");
    assert!(!returned.is_empty(), "parser requires return");
    if workflow.saga {
        if !context.dispatches.is_empty()
            || !context.all_scopes.is_empty()
            || !context.each_regions.is_empty()
        {
            return Err(HScriptDiagnostic::new(
                file,
                "workflow",
                "non-sequential-saga",
                workflow.span,
                "the initial saga slice must be a sequential Action pipeline",
            ));
        }
        for step in &context.steps {
            let action = catalog
                .action(step.action)
                .expect("resolved workflow Action exists");
            if !matches!(action.compensation, Compensation::Action(_)) {
                return Err(HScriptDiagnostic::new(
                    file,
                    "workflow",
                    "missing-saga-compensation",
                    step.invocation_span,
                    format!(
                        "Action {} declares no compensation and cannot enter a saga",
                        catalog.action_name(step.action)
                    ),
                ));
            }
        }
    }
    let mut builder = GraphBuilder::new(graph_name, input_type, success_type, error_types);
    if workflow.saga {
        builder.set_root_saga();
    }
    if let Some(duration_ms) = workflow.deadline_ms {
        builder.set_root_deadline(duration_ms);
    }
    let node_ids = context
        .steps
        .iter()
        .map(|step| {
            builder.add_action_at(
                step.action,
                Some(source_location(file, step.invocation_span)),
            )
        })
        .collect::<Vec<_>>();
    let dispatch_ids = context
        .dispatches
        .iter()
        .map(|dispatch| {
            builder.add_dispatch_at(dispatch.variant, Some(source_location(file, dispatch.span)))
        })
        .collect::<Vec<_>>();
    let all_ids = context
        .all_scopes
        .iter()
        .map(|all| {
            let location = Some(source_location(file, all.span));
            let fork = builder.add_fork_at(
                all.input_type,
                u8::try_from(all.branch_outputs.len()).expect("all branch bound fits u8"),
                location.clone(),
            );
            let branch_types = all
                .branch_outputs
                .iter()
                .map(|(_, type_id)| *type_id)
                .collect::<Vec<_>>();
            let join = builder.add_join_at(&branch_types, location);
            (fork, join)
        })
        .collect::<Vec<_>>();
    for deadline in &context.deadlines {
        builder.add_deadline_region(
            deadline.duration_ms,
            1,
            node_ids[deadline.first_step],
            u16::try_from(deadline.step_count).expect("workflow Action bound fits u16"),
        );
    }
    let each_ids = context
        .each_regions
        .iter()
        .map(|region| {
            builder.add_each_region(EachRegion {
                template: node_ids[region.template_step],
                source_list: region.source_list,
                item_input: region.item_input,
                item_output: region.item_output,
                collected: region.collected,
                bound: region.bound,
                concurrency: region.concurrency,
            })
        })
        .collect::<Vec<_>>();
    let success_terminal = builder.add_terminal_at(
        TerminalKind::Success,
        Some(source_location(file, workflow.success.span)),
    );
    let failure_terminal = builder.add_terminal_at(
        TerminalKind::KnownFailure,
        Some(source_location(file, workflow.errors_span)),
    );
    let not_sent_terminal = builder.add_terminal_at(
        TerminalKind::NotSent,
        Some(source_location(file, workflow.span)),
    );
    let unknown_terminal = builder.add_terminal_at(
        TerminalKind::Unknown,
        Some(source_location(file, workflow.span)),
    );
    for (index, step) in context.steps.iter().enumerate() {
        let source = match step.input {
            ValueOrigin::WorkflowInput => EdgeSource::WorkflowInput,
            ValueOrigin::ActionSuccess(source_index) => {
                EdgeSource::ActionSuccess(node_ids[source_index])
            }
            ValueOrigin::DispatchCase(dispatch_index, tag) => {
                EdgeSource::DispatchCase(dispatch_ids[dispatch_index], tag)
            }
            ValueOrigin::ForkBranch(all_index, branch) => {
                EdgeSource::ForkBranch(all_ids[all_index].0, branch)
            }
            ValueOrigin::JoinField(all_index, field) => {
                EdgeSource::JoinField(all_ids[all_index].1, field)
            }
            ValueOrigin::EachItem(each_index) => EdgeSource::EachItem(each_ids[each_index]),
            ValueOrigin::EachOutput(each_index) => EdgeSource::EachOutput(each_ids[each_index]),
        };
        builder.connect_at(
            source,
            EdgeTarget::ActionInput(node_ids[index]),
            step.presentation,
            Some(source_location(file, step.connection_span)),
        );
        builder.connect_at(
            EdgeSource::ActionError(node_ids[index]),
            EdgeTarget::Terminal(failure_terminal),
            None,
            Some(source_location(file, step.invocation_span)),
        );
        builder.connect_at(
            EdgeSource::ActionNotSent(node_ids[index]),
            EdgeTarget::Terminal(not_sent_terminal),
            None,
            Some(source_location(file, step.invocation_span)),
        );
        builder.connect_at(
            EdgeSource::ActionUnknown(node_ids[index]),
            EdgeTarget::Terminal(unknown_terminal),
            None,
            Some(source_location(file, step.invocation_span)),
        );
    }
    for (index, dispatch) in context.dispatches.iter().enumerate() {
        let source = match dispatch.input {
            ValueOrigin::WorkflowInput => EdgeSource::WorkflowInput,
            ValueOrigin::ActionSuccess(step) => EdgeSource::ActionSuccess(node_ids[step]),
            ValueOrigin::DispatchCase(parent, tag) => {
                EdgeSource::DispatchCase(dispatch_ids[parent], tag)
            }
            ValueOrigin::ForkBranch(all_index, branch) => {
                EdgeSource::ForkBranch(all_ids[all_index].0, branch)
            }
            ValueOrigin::JoinField(all_index, field) => {
                EdgeSource::JoinField(all_ids[all_index].1, field)
            }
            ValueOrigin::EachItem(each_index) => EdgeSource::EachItem(each_ids[each_index]),
            ValueOrigin::EachOutput(each_index) => EdgeSource::EachOutput(each_ids[each_index]),
        };
        builder.connect_at(
            source,
            EdgeTarget::DispatchInput(dispatch_ids[index]),
            None,
            Some(source_location(file, dispatch.span)),
        );
    }
    for (index, all) in context.all_scopes.iter().enumerate() {
        let input_source = match all.input {
            ValueOrigin::WorkflowInput => EdgeSource::WorkflowInput,
            ValueOrigin::ActionSuccess(step) => EdgeSource::ActionSuccess(node_ids[step]),
            ValueOrigin::DispatchCase(dispatch, tag) => {
                EdgeSource::DispatchCase(dispatch_ids[dispatch], tag)
            }
            ValueOrigin::ForkBranch(parent, branch) => {
                EdgeSource::ForkBranch(all_ids[parent].0, branch)
            }
            ValueOrigin::JoinField(parent, field) => {
                EdgeSource::JoinField(all_ids[parent].1, field)
            }
            ValueOrigin::EachItem(each_index) => EdgeSource::EachItem(each_ids[each_index]),
            ValueOrigin::EachOutput(each_index) => EdgeSource::EachOutput(each_ids[each_index]),
        };
        builder.connect_at(
            input_source,
            EdgeTarget::ForkInput(all_ids[index].0),
            None,
            Some(source_location(file, all.span)),
        );
        for (branch, (origin, _)) in all.branch_outputs.iter().enumerate() {
            let branch_source = match origin {
                ValueOrigin::ActionSuccess(step) => EdgeSource::ActionSuccess(node_ids[*step]),
                _ => {
                    return Err(HScriptDiagnostic::new(
                        file,
                        "workflow",
                        "invalid-all-branch",
                        all.span,
                        "every `all` branch must invoke at least one Action",
                    ));
                }
            };
            builder.connect_at(
                branch_source,
                EdgeTarget::JoinInput(
                    all_ids[index].1,
                    u8::try_from(branch).expect("all branch bound fits u8"),
                ),
                None,
                Some(source_location(file, all.span)),
            );
        }
    }
    for (index, region) in context.each_regions.iter().enumerate() {
        let input_source = match region.input {
            ValueOrigin::WorkflowInput => EdgeSource::WorkflowInput,
            ValueOrigin::ActionSuccess(step) => EdgeSource::ActionSuccess(node_ids[step]),
            ValueOrigin::DispatchCase(dispatch, tag) => {
                EdgeSource::DispatchCase(dispatch_ids[dispatch], tag)
            }
            ValueOrigin::ForkBranch(all, branch) => EdgeSource::ForkBranch(all_ids[all].0, branch),
            ValueOrigin::JoinField(all, field) => EdgeSource::JoinField(all_ids[all].1, field),
            ValueOrigin::EachItem(parent) => EdgeSource::EachItem(each_ids[parent]),
            ValueOrigin::EachOutput(parent) => EdgeSource::EachOutput(each_ids[parent]),
        };
        builder.connect_at(
            input_source,
            EdgeTarget::EachInput(each_ids[index]),
            None,
            Some(source_location(file, region.span)),
        );
        builder.connect_at(
            EdgeSource::ActionSuccess(node_ids[region.template_step]),
            EdgeTarget::EachCollect(each_ids[index]),
            None,
            Some(source_location(file, region.span)),
        );
    }
    for (return_origin, return_span) in returned {
        let return_source = match return_origin {
            ValueOrigin::WorkflowInput => {
                return Err(HScriptDiagnostic::new(
                    file,
                    "workflow",
                    "invalid-return",
                    return_span,
                    "workflow input cannot be returned without an Action",
                ));
            }
            ValueOrigin::ActionSuccess(index) => EdgeSource::ActionSuccess(node_ids[index]),
            ValueOrigin::DispatchCase(_, _) => {
                return Err(HScriptDiagnostic::new(
                    file,
                    "workflow",
                    "invalid-return",
                    return_span,
                    "a raw variant case cannot be returned without an Action",
                ));
            }
            ValueOrigin::ForkBranch(_, _) => {
                return Err(HScriptDiagnostic::new(
                    file,
                    "workflow",
                    "invalid-return",
                    return_span,
                    "a raw `all` branch input cannot be returned",
                ));
            }
            ValueOrigin::JoinField(index, field) => EdgeSource::JoinField(all_ids[index].1, field),
            ValueOrigin::EachItem(_) => {
                return Err(HScriptDiagnostic::new(
                    file,
                    "workflow",
                    "invalid-return",
                    return_span,
                    "a raw Each item cannot be returned",
                ));
            }
            ValueOrigin::EachOutput(index) => EdgeSource::EachOutput(each_ids[index]),
        };
        builder.connect_at(
            return_source,
            EdgeTarget::Terminal(success_terminal),
            None,
            Some(source_location(file, return_span)),
        );
    }
    builder.finish(catalog).map_err(|error| {
        HScriptDiagnostic::new(
            file,
            "graph",
            "invalid-graph",
            workflow.span,
            error.to_string(),
        )
    })
}

pub struct CompiledHScriptModule {
    name: Option<String>,
    workflows: Vec<VerifiedGraph>,
}

impl CompiledHScriptModule {
    pub fn name(&self) -> Option<&str> {
        self.name.as_deref()
    }

    pub fn workflows(&self) -> &[VerifiedGraph] {
        &self.workflows
    }

    pub fn workflow(&self, name: &str) -> Option<&VerifiedGraph> {
        self.workflows.iter().find(|workflow| {
            workflow.name() == name
                || workflow
                    .name()
                    .rsplit_once("::")
                    .is_some_and(|(_, local)| local == name)
        })
    }

    pub fn into_workflows(self) -> Vec<VerifiedGraph> {
        self.workflows
    }
}

pub fn compile_hscript_module(
    catalog: &Catalog,
    file: &str,
    source: &str,
) -> Result<CompiledHScriptModule, HScriptDiagnostic> {
    let module = parse_module(file, source)?;
    let workflows = module
        .workflows
        .iter()
        .map(|workflow| {
            compile_workflow(
                catalog,
                file,
                workflow,
                &qualified_workflow_name(&module, workflow),
            )
        })
        .collect::<Result<Vec<_>, _>>()?;
    Ok(CompiledHScriptModule {
        name: module.name,
        workflows,
    })
}

pub fn compile_hscript_workflow(
    catalog: &Catalog,
    file: &str,
    source: &str,
    workflow_name: &str,
) -> Result<VerifiedGraph, HScriptDiagnostic> {
    let module = parse_module(file, source)?;
    let selected = module
        .workflows
        .iter()
        .position(|workflow| {
            workflow.name == workflow_name
                || qualified_workflow_name(&module, workflow) == workflow_name
        })
        .ok_or_else(|| {
            HScriptDiagnostic::new(
                file,
                "module",
                "unknown-workflow",
                module.workflows[0].span,
                format!("module does not declare workflow `{workflow_name}`"),
            )
        })?;
    let compiled = module
        .workflows
        .iter()
        .map(|workflow| {
            compile_workflow(
                catalog,
                file,
                workflow,
                &qualified_workflow_name(&module, workflow),
            )
        })
        .collect::<Result<Vec<_>, _>>()?;
    Ok(compiled
        .into_iter()
        .nth(selected)
        .expect("selected workflow index exists"))
}

pub fn compile_hscript(
    catalog: &Catalog,
    file: &str,
    source: &str,
) -> Result<VerifiedGraph, HScriptDiagnostic> {
    let module = parse_module(file, source)?;
    if module.workflows.len() != 1 {
        return Err(HScriptDiagnostic::new(
            file,
            "module",
            "workflow-selection-required",
            module.workflows[1].name_span,
            "this module declares multiple workflows; select one explicitly",
        ));
    }
    compile_workflow(
        catalog,
        file,
        &module.workflows[0],
        &qualified_workflow_name(&module, &module.workflows[0]),
    )
}
