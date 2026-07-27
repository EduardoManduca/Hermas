use std::collections::{BTreeMap, BTreeSet};
use std::error::Error;
use std::fmt;

use sha2::{Digest, Sha256};

use crate::catalog::{
    ActionDeclaration, ActionId, ActionKindDeclaration, AppId, Catalog, CatalogError,
    Representation, TypeId,
};

const MAX_TYPES_PER_APP: usize = 256;
const MAX_ACTIONS_PER_APP: usize = 256;
const MAX_REPRESENTATION_DEPTH: usize = 64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Span {
    pub offset: usize,
    pub length: usize,
    pub line: usize,
    pub column: usize,
}

impl Span {
    const fn start() -> Self {
        Self {
            offset: 0,
            length: 0,
            line: 1,
            column: 1,
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SchemaDiagnostic {
    pub file: String,
    pub stage: &'static str,
    pub code: &'static str,
    pub span: Span,
    pub message: String,
}

impl SchemaDiagnostic {
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

impl fmt::Display for SchemaDiagnostic {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "{}:{}:{}: {}/{}: {}",
            self.file, self.span.line, self.span.column, self.stage, self.code, self.message
        )
    }
}

impl Error for SchemaDiagnostic {}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ContractFingerprint([u8; 32]);

impl ContractFingerprint {
    pub const fn as_bytes(&self) -> &[u8; 32] {
        &self.0
    }
}

impl fmt::Display for ContractFingerprint {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        for byte in self.0 {
            write!(formatter, "{byte:02x}")?;
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SchemaContract {
    pub app: AppId,
    pub types: BTreeMap<String, TypeId>,
    pub actions: BTreeMap<String, ActionId>,
    pub fingerprint: ContractFingerprint,
}

impl SchemaContract {
    pub fn type_id(&self, name: &str) -> Option<TypeId> {
        self.types.get(name).copied()
    }

    pub fn action_id(&self, name: &str) -> Option<ActionId> {
        self.actions.get(name).copied()
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Symbol {
    LeftBrace,
    RightBrace,
    LeftAngle,
    RightAngle,
    Colon,
    Equals,
}

#[derive(Clone, Debug, Eq, PartialEq)]
enum TokenKind {
    Identifier(String),
    Number(u32),
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

fn lex(file: &str, source: &str) -> Result<Vec<Token>, SchemaDiagnostic> {
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
            let mut value = 0u32;
            while offset < bytes.len() && bytes[offset].is_ascii_digit() {
                value = value
                    .checked_mul(10)
                    .and_then(|item| item.checked_add(u32::from(bytes[offset] - b'0')))
                    .ok_or_else(|| {
                        SchemaDiagnostic::new(
                            file,
                            "syntax",
                            "invalid-integer",
                            start,
                            "integer literal exceeds the 32-bit range",
                        )
                    })?;
                offset += 1;
                column += 1;
            }
            tokens.push(Token {
                kind: TokenKind::Number(value),
                span: Span {
                    length: offset - begin,
                    ..start
                },
            });
            continue;
        }
        let symbol = match byte {
            b'{' => Symbol::LeftBrace,
            b'}' => Symbol::RightBrace,
            b'<' => Symbol::LeftAngle,
            b'>' => Symbol::RightAngle,
            b':' => Symbol::Colon,
            b'=' => Symbol::Equals,
            _ => {
                return Err(SchemaDiagnostic::new(
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
            span: start,
        });
        offset += 1;
        column += 1;
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
    fn from_source(file: &str, source: &str) -> Result<Self, SchemaDiagnostic> {
        Ok(Self {
            file: file.to_owned(),
            items: lex(file, source)?,
            index: 0,
        })
    }

    fn peek(&self) -> &Token {
        &self.items[self.index]
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

    fn expect_keyword(&mut self, expected: &'static str) -> Result<Span, SchemaDiagnostic> {
        let token = self.advance();
        match token.kind {
            TokenKind::Identifier(value) if value == expected => Ok(token.span),
            _ => Err(self.unexpected(token.span, format!("expected `{expected}`"))),
        }
    }

    fn expect_identifier(&mut self) -> Result<(String, Span), SchemaDiagnostic> {
        let token = self.advance();
        match token.kind {
            TokenKind::Identifier(value) => Ok((value, token.span)),
            _ => Err(self.unexpected(token.span, "expected identifier")),
        }
    }

    fn expect_number(&mut self) -> Result<(u32, Span), SchemaDiagnostic> {
        let token = self.advance();
        match token.kind {
            TokenKind::Number(value) => Ok((value, token.span)),
            _ => Err(self.unexpected(token.span, "expected integer")),
        }
    }

    fn expect_symbol(&mut self, expected: Symbol) -> Result<Span, SchemaDiagnostic> {
        let token = self.advance();
        match token.kind {
            TokenKind::Symbol(value) if value == expected => Ok(token.span),
            _ => Err(self.unexpected(token.span, format!("expected {expected:?}"))),
        }
    }

    fn expect_end(&self) -> Result<(), SchemaDiagnostic> {
        if matches!(self.peek().kind, TokenKind::End) {
            Ok(())
        } else {
            Err(self.unexpected(self.peek().span, "expected end of file"))
        }
    }

    fn diagnostic(
        &self,
        stage: &'static str,
        code: &'static str,
        span: Span,
        message: impl Into<String>,
    ) -> SchemaDiagnostic {
        SchemaDiagnostic::new(&self.file, stage, code, span, message)
    }

    fn unexpected(&self, span: Span, message: impl Into<String>) -> SchemaDiagnostic {
        self.diagnostic("syntax", "unexpected-token", span, message)
    }
}

#[derive(Clone, Debug)]
enum TypeExpression {
    Unit,
    Integer,
    Boolean,
    String(u32),
    Bytes(u32),
    Named(String, Span),
    Record(BTreeMap<String, TypeExpression>),
    List(Box<TypeExpression>, u16),
    Variant(BTreeMap<String, TypeExpression>),
}

#[derive(Clone, Debug)]
struct TypeDeclaration {
    expression: TypeExpression,
    span: Span,
}

#[derive(Clone, Debug)]
enum ParsedActionKind {
    Irreversible,
    Reversible { compensation: String },
}

#[derive(Clone, Debug)]
struct ParsedAction {
    input: String,
    success: String,
    error: String,
    kind: ParsedActionKind,
    span: Span,
}

#[derive(Clone, Debug)]
struct ParsedSchema {
    app: String,
    app_span: Span,
    types: BTreeMap<String, TypeDeclaration>,
    actions: BTreeMap<String, ParsedAction>,
}

fn parse_type_expression(
    tokens: &mut Tokens,
    depth: usize,
) -> Result<TypeExpression, SchemaDiagnostic> {
    if depth > MAX_REPRESENTATION_DEPTH {
        return Err(tokens.diagnostic(
            "schema",
            "limit-exceeded",
            tokens.peek().span,
            format!("representation nesting exceeds {MAX_REPRESENTATION_DEPTH}"),
        ));
    }
    let (name, span) = tokens.expect_identifier()?;
    match name.as_str() {
        "Unit" => Ok(TypeExpression::Unit),
        "Integer" => Ok(TypeExpression::Integer),
        "Boolean" => Ok(TypeExpression::Boolean),
        "String" | "Bytes" => {
            tokens.expect_symbol(Symbol::LeftAngle)?;
            let (maximum, _) = tokens.expect_number()?;
            tokens.expect_symbol(Symbol::RightAngle)?;
            if name == "String" {
                Ok(TypeExpression::String(maximum))
            } else {
                Ok(TypeExpression::Bytes(maximum))
            }
        }
        "List" => {
            tokens.expect_symbol(Symbol::LeftAngle)?;
            let element = parse_type_expression(tokens, depth + 1)?;
            let (maximum, maximum_span) = tokens.expect_number()?;
            let maximum = u16::try_from(maximum).map_err(|_| {
                tokens.diagnostic(
                    "schema",
                    "invalid-bound",
                    maximum_span,
                    "list bound exceeds the 16-bit representation",
                )
            })?;
            tokens.expect_symbol(Symbol::RightAngle)?;
            Ok(TypeExpression::List(Box::new(element), maximum))
        }
        "record" => parse_members(tokens, depth, false).map(TypeExpression::Record),
        "variant" => parse_members(tokens, depth, true).map(TypeExpression::Variant),
        _ => Ok(TypeExpression::Named(name, span)),
    }
}

fn parse_members(
    tokens: &mut Tokens,
    depth: usize,
    require_member: bool,
) -> Result<BTreeMap<String, TypeExpression>, SchemaDiagnostic> {
    tokens.expect_symbol(Symbol::LeftBrace)?;
    let mut members = BTreeMap::new();
    while !matches!(
        tokens.peek().kind,
        TokenKind::Symbol(Symbol::RightBrace) | TokenKind::End
    ) {
        let (name, span) = tokens.expect_identifier()?;
        tokens.expect_symbol(Symbol::Colon)?;
        let representation = parse_type_expression(tokens, depth + 1)?;
        if members.insert(name.clone(), representation).is_some() {
            return Err(tokens.diagnostic(
                "schema",
                "duplicate-member",
                span,
                format!("member `{name}` is declared twice"),
            ));
        }
    }
    tokens.expect_symbol(Symbol::RightBrace)?;
    if require_member && members.is_empty() {
        return Err(tokens.diagnostic(
            "schema",
            "empty-variant",
            tokens.peek().span,
            "a variant must declare at least one case",
        ));
    }
    Ok(members)
}

fn parse_action(tokens: &mut Tokens) -> Result<(String, ParsedAction), SchemaDiagnostic> {
    let action_span = tokens.expect_keyword("action")?;
    let (name, _) = tokens.expect_identifier()?;
    tokens.expect_symbol(Symbol::LeftBrace)?;
    tokens.expect_keyword("input")?;
    let (input, _) = tokens.expect_identifier()?;
    tokens.expect_keyword("success")?;
    let (success, _) = tokens.expect_identifier()?;
    tokens.expect_keyword("error")?;
    let (error, _) = tokens.expect_identifier()?;
    tokens.expect_keyword("kind")?;
    let (kind_name, kind_span) = tokens.expect_identifier()?;
    let kind = match kind_name.as_str() {
        "irreversible" => ParsedActionKind::Irreversible,
        "reversible" => {
            tokens.expect_keyword("compensate")?;
            let (compensation, _) = tokens.expect_identifier()?;
            ParsedActionKind::Reversible { compensation }
        }
        _ => {
            return Err(tokens.diagnostic(
                "schema",
                "invalid-action-kind",
                kind_span,
                "Action kind must be `irreversible` or `reversible`",
            ));
        }
    };
    tokens.expect_symbol(Symbol::RightBrace)?;
    Ok((
        name,
        ParsedAction {
            input,
            success,
            error,
            kind,
            span: action_span,
        },
    ))
}

fn parse(file: &str, source: &str) -> Result<ParsedSchema, SchemaDiagnostic> {
    let mut tokens = Tokens::from_source(file, source)?;
    let app_span = tokens.expect_keyword("app")?;
    let (app, _) = tokens.expect_identifier()?;
    let mut types = BTreeMap::new();
    let mut actions = BTreeMap::new();
    while !matches!(tokens.peek().kind, TokenKind::End) {
        if tokens.check_keyword("type") {
            let type_span = tokens.expect_keyword("type")?;
            let (name, name_span) = tokens.expect_identifier()?;
            tokens.expect_symbol(Symbol::Equals)?;
            let expression = parse_type_expression(&mut tokens, 0)?;
            if types
                .insert(
                    name.clone(),
                    TypeDeclaration {
                        expression,
                        span: type_span,
                    },
                )
                .is_some()
            {
                return Err(tokens.diagnostic(
                    "schema",
                    "duplicate-type",
                    name_span,
                    format!("type `{name}` is declared twice"),
                ));
            }
            if types.len() > MAX_TYPES_PER_APP {
                return Err(tokens.diagnostic(
                    "schema",
                    "limit-exceeded",
                    type_span,
                    format!("an app may declare at most {MAX_TYPES_PER_APP} types"),
                ));
            }
        } else if tokens.check_keyword("action") {
            let (name, action) = parse_action(&mut tokens)?;
            let span = action.span;
            if actions.insert(name.clone(), action).is_some() {
                return Err(tokens.diagnostic(
                    "schema",
                    "duplicate-action",
                    span,
                    format!("Action `{name}` is declared twice"),
                ));
            }
            if actions.len() > MAX_ACTIONS_PER_APP {
                return Err(tokens.diagnostic(
                    "schema",
                    "limit-exceeded",
                    span,
                    format!("an app may declare at most {MAX_ACTIONS_PER_APP} Actions"),
                ));
            }
        } else {
            return Err(tokens.unexpected(tokens.peek().span, "expected `type` or `action`"));
        }
    }
    tokens.expect_end()?;
    if types.is_empty() {
        return Err(tokens.diagnostic(
            "schema",
            "missing-type",
            app_span,
            "an app schema must declare at least one nominal type",
        ));
    }
    if actions.is_empty() {
        return Err(tokens.diagnostic(
            "schema",
            "missing-action",
            app_span,
            "an app schema must declare at least one Action",
        ));
    }
    Ok(ParsedSchema {
        app,
        app_span,
        types,
        actions,
    })
}

fn resolve_expression(
    file: &str,
    expression: &TypeExpression,
    declarations: &BTreeMap<String, TypeDeclaration>,
    resolved: &mut BTreeMap<String, Representation>,
    visiting: &mut BTreeSet<String>,
    depth: usize,
) -> Result<Representation, SchemaDiagnostic> {
    if depth > MAX_REPRESENTATION_DEPTH {
        return Err(SchemaDiagnostic::new(
            file,
            "schema",
            "limit-exceeded",
            Span::start(),
            format!("representation nesting exceeds {MAX_REPRESENTATION_DEPTH}"),
        ));
    }
    match expression {
        TypeExpression::Unit => Ok(Representation::Unit),
        TypeExpression::Integer => Ok(Representation::Integer),
        TypeExpression::Boolean => Ok(Representation::Boolean),
        TypeExpression::String(maximum) => Ok(Representation::String { maximum: *maximum }),
        TypeExpression::Bytes(maximum) => Ok(Representation::Bytes { maximum: *maximum }),
        TypeExpression::Named(name, span) => {
            if let Some(representation) = resolved.get(name) {
                return Ok(representation.clone());
            }
            let declaration = declarations.get(name).ok_or_else(|| {
                SchemaDiagnostic::new(
                    file,
                    "schema",
                    "unknown-type",
                    *span,
                    format!("unknown type `{name}`"),
                )
            })?;
            if !visiting.insert(name.clone()) {
                return Err(SchemaDiagnostic::new(
                    file,
                    "schema",
                    "recursive-representation",
                    *span,
                    format!("type `{name}` has a recursive representation"),
                ));
            }
            let representation = resolve_expression(
                file,
                &declaration.expression,
                declarations,
                resolved,
                visiting,
                depth + 1,
            )?;
            visiting.remove(name);
            resolved.insert(name.clone(), representation.clone());
            Ok(representation)
        }
        TypeExpression::Record(fields) => fields
            .iter()
            .map(|(name, field)| {
                resolve_expression(file, field, declarations, resolved, visiting, depth + 1)
                    .map(|representation| (name.clone(), representation))
            })
            .collect::<Result<BTreeMap<_, _>, _>>()
            .map(Representation::Record),
        TypeExpression::List(element, maximum) => {
            resolve_expression(file, element, declarations, resolved, visiting, depth + 1)
                .map(|element| Representation::list(element, *maximum))
        }
        TypeExpression::Variant(cases) => cases
            .iter()
            .map(|(name, payload)| {
                resolve_expression(file, payload, declarations, resolved, visiting, depth + 1)
                    .map(|representation| (name.clone(), representation))
            })
            .collect::<Result<BTreeMap<_, _>, _>>()
            .map(Representation::Variant),
    }
}

fn resolve_types(
    file: &str,
    parsed: &ParsedSchema,
) -> Result<BTreeMap<String, Representation>, SchemaDiagnostic> {
    let mut resolved = BTreeMap::new();
    for (name, declaration) in &parsed.types {
        if resolved.contains_key(name) {
            continue;
        }
        let mut visiting = BTreeSet::from([name.clone()]);
        let representation = resolve_expression(
            file,
            &declaration.expression,
            &parsed.types,
            &mut resolved,
            &mut visiting,
            0,
        )?;
        resolved.insert(name.clone(), representation);
    }
    Ok(resolved)
}

fn write_type_expression(output: &mut String, expression: &TypeExpression) {
    match expression {
        TypeExpression::Unit => output.push_str("unit"),
        TypeExpression::Integer => output.push_str("integer"),
        TypeExpression::Boolean => output.push_str("boolean"),
        TypeExpression::String(maximum) => output.push_str(&format!("string<{maximum}>")),
        TypeExpression::Bytes(maximum) => output.push_str(&format!("bytes<{maximum}>")),
        TypeExpression::Named(name, _) => {
            output.push_str("named:");
            output.push_str(name);
        }
        TypeExpression::Record(fields) => {
            output.push_str("record{");
            for (name, field) in fields {
                output.push_str(name);
                output.push(':');
                write_type_expression(output, field);
                output.push(';');
            }
            output.push('}');
        }
        TypeExpression::List(element, maximum) => {
            output.push_str("list<");
            write_type_expression(output, element);
            output.push_str(&format!(",{maximum}>"));
        }
        TypeExpression::Variant(cases) => {
            output.push_str("variant{");
            for (name, payload) in cases {
                output.push_str(name);
                output.push(':');
                write_type_expression(output, payload);
                output.push(';');
            }
            output.push('}');
        }
    }
}

fn fingerprint(parsed: &ParsedSchema) -> ContractFingerprint {
    let mut canonical = format!("hermas2-contract-v1\napp:{}\n", parsed.app);
    for (name, declaration) in &parsed.types {
        canonical.push_str("type:");
        canonical.push_str(name);
        canonical.push('=');
        write_type_expression(&mut canonical, &declaration.expression);
        canonical.push('\n');
    }
    for (name, action) in &parsed.actions {
        canonical.push_str(&format!(
            "action:{name}:{}->{}!{}:",
            action.input, action.success, action.error
        ));
        match &action.kind {
            ParsedActionKind::Irreversible => canonical.push_str("irreversible"),
            ParsedActionKind::Reversible { compensation } => {
                canonical.push_str("reversible:");
                canonical.push_str(compensation);
            }
        }
        canonical.push('\n');
    }
    ContractFingerprint(Sha256::digest(canonical.as_bytes()).into())
}

pub fn compile_schema(
    catalog: &mut Catalog,
    file: &str,
    source: &str,
) -> Result<SchemaContract, SchemaDiagnostic> {
    let parsed = parse(file, source)?;
    let representations = resolve_types(file, &parsed)?;
    let contract_fingerprint = fingerprint(&parsed);
    let mut candidate = catalog.clone();
    let app = candidate.declare_app(&parsed.app).map_err(|error| {
        SchemaDiagnostic::new(
            file,
            "catalog",
            "invalid-app",
            parsed.app_span,
            error.to_string(),
        )
    })?;
    candidate
        .set_app_fingerprint(app, *contract_fingerprint.as_bytes())
        .expect("newly declared app accepts its fingerprint");
    let mut types = BTreeMap::new();
    for (name, representation) in representations {
        let span = parsed.types[&name].span;
        let type_id = candidate
            .declare_type(app, &name, representation)
            .map_err(|error| {
                SchemaDiagnostic::new(file, "catalog", "invalid-type", span, error.to_string())
            })?;
        types.insert(name, type_id);
    }
    for (name, declaration) in &parsed.types {
        let TypeExpression::Variant(cases) = &declaration.expression else {
            continue;
        };
        let case_types = cases
            .iter()
            .map(|(case, payload)| {
                let TypeExpression::Named(payload_name, _) = payload else {
                    return None;
                };
                types
                    .get(payload_name)
                    .copied()
                    .map(|type_id| (case.clone(), type_id))
            })
            .collect::<Option<BTreeMap<_, _>>>();
        if let Some(case_types) = case_types {
            candidate
                .set_variant_cases(types[name], case_types)
                .map_err(|error| {
                    SchemaDiagnostic::new(
                        file,
                        "catalog",
                        "invalid-variant",
                        declaration.span,
                        error.to_string(),
                    )
                })?;
        }
    }
    for (name, declaration) in &parsed.types {
        let TypeExpression::List(element, _) = &declaration.expression else {
            continue;
        };
        let TypeExpression::Named(element_name, _) = element.as_ref() else {
            continue;
        };
        if let Some(element_type) = types.get(element_name).copied() {
            candidate
                .set_list_element(types[name], element_type)
                .map_err(|error| {
                    SchemaDiagnostic::new(
                        file,
                        "catalog",
                        "invalid-list",
                        declaration.span,
                        error.to_string(),
                    )
                })?;
        }
    }
    let mut declarations = Vec::with_capacity(parsed.actions.len());
    for (name, action) in &parsed.actions {
        let resolve = |type_name: &str| {
            types.get(type_name).copied().ok_or_else(|| {
                SchemaDiagnostic::new(
                    file,
                    "schema",
                    "unknown-action-type",
                    action.span,
                    format!("Action `{name}` references unknown type `{type_name}`"),
                )
            })
        };
        declarations.push(ActionDeclaration {
            name: name.clone(),
            input: resolve(&action.input)?,
            success: resolve(&action.success)?,
            error: resolve(&action.error)?,
            kind: match &action.kind {
                ParsedActionKind::Irreversible => ActionKindDeclaration::Irreversible,
                ParsedActionKind::Reversible { compensation } => {
                    ActionKindDeclaration::Reversible {
                        compensation: compensation.clone(),
                    }
                }
            },
        });
    }
    let action_ids = candidate
        .declare_actions(app, declarations)
        .map_err(|error| {
            let action_span = match &error {
                CatalogError::DuplicateAction { name, .. } => parsed.actions.get(name),
                CatalogError::UnknownCompensation { action, .. }
                | CatalogError::IncompatibleCompensation { action, .. } => {
                    parsed.actions.get(action)
                }
                CatalogError::SelfCompensation(action) => parsed.actions.get(action),
                _ => None,
            }
            .map_or(parsed.app_span, |action| action.span);
            SchemaDiagnostic::new(
                file,
                "catalog",
                "invalid-action",
                action_span,
                error.to_string(),
            )
        })?;
    let actions = parsed.actions.keys().cloned().zip(action_ids).collect();
    *catalog = candidate;
    Ok(SchemaContract {
        app,
        types,
        actions,
        fingerprint: contract_fingerprint,
    })
}
