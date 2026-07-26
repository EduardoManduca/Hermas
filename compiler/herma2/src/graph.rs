use std::collections::{BTreeSet, VecDeque};
use std::error::Error;
use std::fmt;

use crate::catalog::{ActionId, Catalog, TypeId};

pub const MAX_GRAPH_NODES: usize = 64;
pub const MAX_GRAPH_EDGES: usize = 192;
pub const MAX_ALL_BRANCHES: usize = 8;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SourceLocation {
    pub file: String,
    pub offset: usize,
    pub length: usize,
    pub line: usize,
    pub column: usize,
}

impl fmt::Display for SourceLocation {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{}:{}:{}", self.file, self.line, self.column)
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct NodeId(u16);

impl NodeId {
    pub const fn raw(self) -> u16 {
        self.0
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TerminalKind {
    Success,
    KnownFailure,
    NotSent,
    Unknown,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum NodeKind {
    Action(ActionId),
    Dispatch(TypeId),
    Fork(TypeId, u8),
    Join([Option<TypeId>; MAX_ALL_BRANCHES]),
    Terminal(TerminalKind),
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum EdgeSource {
    WorkflowInput,
    ActionSuccess(NodeId),
    ActionError(NodeId),
    ActionNotSent(NodeId),
    ActionUnknown(NodeId),
    DispatchCase(NodeId, u8),
    ForkBranch(NodeId, u8),
    JoinField(NodeId, u8),
    EachItem(u8),
    EachOutput(u8),
}

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub enum EdgeTarget {
    ActionInput(NodeId),
    DispatchInput(NodeId),
    ForkInput(NodeId),
    JoinInput(NodeId, u8),
    EachInput(u8),
    EachCollect(u8),
    Terminal(NodeId),
}

#[derive(Clone, Debug, Eq, PartialEq)]
struct Edge {
    source: EdgeSource,
    target: EdgeTarget,
    presentation: Option<TypeId>,
    source_location: Option<SourceLocation>,
}

#[derive(Clone, Copy, Debug)]
pub(crate) struct DeadlineRegion {
    pub duration_ms: u64,
    pub parent: u16,
    pub first_node: u16,
    pub node_count: u16,
}

#[derive(Clone, Copy, Debug)]
pub(crate) struct EachRegion {
    pub template: NodeId,
    pub source_list: TypeId,
    pub item_input: TypeId,
    pub item_output: TypeId,
    pub collected: TypeId,
    pub bound: u16,
    pub concurrency: u8,
}

#[derive(Clone, Debug)]
pub struct GraphBuilder {
    name: String,
    input: TypeId,
    success: TypeId,
    errors: Vec<TypeId>,
    nodes: Vec<NodeKind>,
    node_source_locations: Vec<Option<SourceLocation>>,
    edges: Vec<Edge>,
    deadlines: Vec<DeadlineRegion>,
    each_regions: Vec<EachRegion>,
}

impl GraphBuilder {
    pub fn new(name: &str, input: TypeId, success: TypeId, errors: Vec<TypeId>) -> Self {
        Self {
            name: name.to_owned(),
            input,
            success,
            errors,
            nodes: Vec::new(),
            node_source_locations: Vec::new(),
            edges: Vec::new(),
            deadlines: Vec::new(),
            each_regions: Vec::new(),
        }
    }

    pub fn add_action(&mut self, action: ActionId) -> NodeId {
        self.add_action_at(action, None)
    }

    pub fn add_action_at(
        &mut self,
        action: ActionId,
        source_location: Option<SourceLocation>,
    ) -> NodeId {
        self.push_node(NodeKind::Action(action), source_location)
    }

    pub fn add_dispatch(&mut self, variant: TypeId) -> NodeId {
        self.add_dispatch_at(variant, None)
    }

    pub fn add_dispatch_at(
        &mut self,
        variant: TypeId,
        source_location: Option<SourceLocation>,
    ) -> NodeId {
        self.push_node(NodeKind::Dispatch(variant), source_location)
    }

    pub fn add_fork_at(
        &mut self,
        input: TypeId,
        branches: u8,
        source_location: Option<SourceLocation>,
    ) -> NodeId {
        self.push_node(NodeKind::Fork(input, branches), source_location)
    }

    pub fn add_join_at(
        &mut self,
        branch_types: &[TypeId],
        source_location: Option<SourceLocation>,
    ) -> NodeId {
        let mut types = [None; MAX_ALL_BRANCHES];
        for (slot, type_id) in branch_types.iter().copied().enumerate() {
            types[slot] = Some(type_id);
        }
        self.push_node(NodeKind::Join(types), source_location)
    }

    pub fn add_terminal(&mut self, kind: TerminalKind) -> NodeId {
        self.add_terminal_at(kind, None)
    }

    pub fn set_root_deadline(&mut self, duration_ms: u64) {
        self.deadlines.push(DeadlineRegion {
            duration_ms,
            parent: 0,
            first_node: 0,
            node_count: 0,
        });
    }

    pub fn add_deadline_region(
        &mut self,
        duration_ms: u64,
        parent: u16,
        first_node: NodeId,
        node_count: u16,
    ) {
        self.deadlines.push(DeadlineRegion {
            duration_ms,
            parent,
            first_node: first_node.raw(),
            node_count,
        });
    }

    pub(crate) fn add_each_region(&mut self, region: EachRegion) -> u8 {
        let id = u8::try_from(self.each_regions.len() + 1).expect("Each region count fits u8");
        self.each_regions.push(region);
        id
    }

    pub fn add_terminal_at(
        &mut self,
        kind: TerminalKind,
        source_location: Option<SourceLocation>,
    ) -> NodeId {
        self.push_node(NodeKind::Terminal(kind), source_location)
    }

    pub fn connect(
        &mut self,
        source: EdgeSource,
        target: EdgeTarget,
        presentation: Option<TypeId>,
    ) {
        self.connect_at(source, target, presentation, None);
    }

    pub fn connect_at(
        &mut self,
        source: EdgeSource,
        target: EdgeTarget,
        presentation: Option<TypeId>,
        source_location: Option<SourceLocation>,
    ) {
        self.edges.push(Edge {
            source,
            target,
            presentation,
            source_location,
        });
    }

    pub fn finish(self, catalog: &Catalog) -> Result<VerifiedGraph, GraphError> {
        verify(&self, catalog)?;
        Ok(VerifiedGraph { graph: self })
    }

    fn push_node(&mut self, kind: NodeKind, source_location: Option<SourceLocation>) -> NodeId {
        let raw = u16::try_from(self.nodes.len() + 1).expect("graph builder node IDs fit in u16");
        self.nodes.push(kind);
        self.node_source_locations.push(source_location);
        NodeId(raw)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum GraphErrorCode {
    LimitExceeded,
    UnknownType,
    UnknownAction,
    DuplicateErrorType,
    MissingTerminal,
    DuplicateTerminal,
    InvalidEndpoint,
    InvalidRoute,
    InvalidPresentation,
    RepresentationMismatch,
    NominalMismatch,
    DuplicateEdge,
    InvalidPortCardinality,
    Cycle,
    UnreachableNode,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct GraphError {
    pub code: GraphErrorCode,
    pub node: Option<NodeId>,
    pub edge: Option<usize>,
    pub message: String,
}

impl GraphError {
    fn graph(code: GraphErrorCode, message: impl Into<String>) -> Self {
        Self {
            code,
            node: None,
            edge: None,
            message: message.into(),
        }
    }

    fn node(code: GraphErrorCode, node: NodeId, message: impl Into<String>) -> Self {
        Self {
            code,
            node: Some(node),
            edge: None,
            message: message.into(),
        }
    }

    fn edge(code: GraphErrorCode, edge: usize, message: impl Into<String>) -> Self {
        Self {
            code,
            node: None,
            edge: Some(edge),
            message: message.into(),
        }
    }
}

impl fmt::Display for GraphError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{:?}: {}", self.code, self.message)
    }
}

impl Error for GraphError {}

#[derive(Clone, Debug)]
pub struct VerifiedGraph {
    graph: GraphBuilder,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ResourceSummary {
    pub action_nodes: usize,
    pub dispatch_nodes: usize,
    pub fork_nodes: usize,
    pub join_nodes: usize,
    pub deadline_regions: usize,
    pub each_regions: usize,
    pub terminal_nodes: usize,
    pub edges: usize,
    pub required_apps: usize,
    pub maximum_concurrent_actions: usize,
    pub maximum_payload_bytes: usize,
}

impl fmt::Display for ResourceSummary {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "actions={} dispatches={} forks={} joins={} deadline_regions={} each_regions={} terminals={} edges={} apps={} max_concurrent_actions={} max_payload_bytes={}",
            self.action_nodes,
            self.dispatch_nodes,
            self.fork_nodes,
            self.join_nodes,
            self.deadline_regions,
            self.each_regions,
            self.terminal_nodes,
            self.edges,
            self.required_apps,
            self.maximum_concurrent_actions,
            self.maximum_payload_bytes
        )
    }
}

impl VerifiedGraph {
    pub fn name(&self) -> &str {
        &self.graph.name
    }

    pub fn resources(&self, catalog: &Catalog) -> ResourceSummary {
        let action_nodes = self
            .graph
            .nodes
            .iter()
            .filter(|node| matches!(node, NodeKind::Action(_)))
            .count();
        let dispatch_nodes = self
            .graph
            .nodes
            .iter()
            .filter(|node| matches!(node, NodeKind::Dispatch(_)))
            .count();
        let fork_nodes = self
            .graph
            .nodes
            .iter()
            .filter(|node| matches!(node, NodeKind::Fork(_, _)))
            .count();
        let join_nodes = self
            .graph
            .nodes
            .iter()
            .filter(|node| matches!(node, NodeKind::Join(_)))
            .count();
        let terminal_nodes = self
            .graph
            .nodes
            .iter()
            .filter(|node| matches!(node, NodeKind::Terminal(_)))
            .count();
        let required_apps: BTreeSet<_> = self
            .graph
            .nodes
            .iter()
            .filter_map(|node| match node {
                NodeKind::Action(action_id) => catalog.action(*action_id).map(|action| action.app),
                NodeKind::Dispatch(_) | NodeKind::Fork(_, _) | NodeKind::Join(_) => None,
                NodeKind::Terminal(_) => None,
            })
            .collect();
        let maximum_payload_bytes = graph_types(&self.graph, catalog)
            .filter_map(|type_id| {
                catalog
                    .type_def(type_id)
                    .and_then(|item| item.representation.maximum_wire_size())
            })
            .max()
            .unwrap_or(0);
        ResourceSummary {
            action_nodes,
            dispatch_nodes,
            fork_nodes,
            join_nodes,
            deadline_regions: self.graph.deadlines.len(),
            each_regions: self.graph.each_regions.len(),
            terminal_nodes,
            edges: self.graph.edges.len(),
            required_apps: required_apps.len(),
            maximum_concurrent_actions: self
                .graph
                .nodes
                .iter()
                .filter_map(|node| match node {
                    NodeKind::Fork(_, branches) => Some(usize::from(*branches)),
                    _ => None,
                })
                .chain(
                    self.graph
                        .each_regions
                        .iter()
                        .map(|region| usize::from(region.concurrency)),
                )
                .max()
                .unwrap_or_else(|| usize::from(action_nodes != 0)),
            maximum_payload_bytes,
        }
    }

    pub fn has_complete_source_map(&self) -> bool {
        self.graph.node_source_locations.iter().all(Option::is_some)
            && self
                .graph
                .edges
                .iter()
                .all(|edge| edge.source_location.is_some())
    }

    pub fn source_map(&self) -> String {
        let mut output = String::new();
        for (index, location) in self.graph.node_source_locations.iter().enumerate() {
            let node = index + 1;
            match location {
                Some(location) => output.push_str(&format!(
                    "node n{node} {}:{}:{} offset={} length={}\n",
                    location.file, location.line, location.column, location.offset, location.length
                )),
                None => output.push_str(&format!("node n{node} <unknown>\n")),
            }
        }
        for (index, edge) in self.graph.edges.iter().enumerate() {
            match &edge.source_location {
                Some(location) => output.push_str(&format!(
                    "edge e{} {}:{}:{} offset={} length={}\n",
                    index + 1,
                    location.file,
                    location.line,
                    location.column,
                    location.offset,
                    location.length
                )),
                None => output.push_str(&format!("edge e{} <unknown>\n", index + 1)),
            }
        }
        output
    }

    pub(crate) fn image_view(&self, catalog: &Catalog) -> GraphImageView {
        let nodes = self
            .graph
            .nodes
            .iter()
            .map(|node| match node {
                NodeKind::Action(action) => ImageNode::Action(*action),
                NodeKind::Dispatch(variant) => ImageNode::Dispatch(*variant),
                NodeKind::Fork(input, branches) => ImageNode::Fork(*input, *branches),
                NodeKind::Join(types) => ImageNode::Join(*types),
                NodeKind::Terminal(terminal) => ImageNode::Terminal(*terminal),
            })
            .collect();
        let edges = self
            .graph
            .edges
            .iter()
            .map(|edge| {
                let source_type = source_value_type(edge.source, &self.graph, catalog)
                    .expect("verified edge has a valid source");
                let target_type = edge.presentation.or(source_type);
                ImageEdge {
                    source: edge.source,
                    target: edge.target,
                    source_type,
                    target_type,
                    presentation: edge.presentation,
                }
            })
            .collect();
        GraphImageView {
            name: self.graph.name.clone(),
            input: self.graph.input,
            success: self.graph.success,
            errors: self.graph.errors.clone(),
            nodes,
            edges,
            deadlines: self.graph.deadlines.clone(),
            each_regions: self.graph.each_regions.clone(),
        }
    }

    pub fn explain(&self, catalog: &Catalog) -> String {
        let mut output = String::new();
        output.push_str(&format!("workflow {}\n", self.graph.name));
        output.push_str(&format!(
            "  input {}\n  success {}\n  errors [{}]\n",
            catalog.type_name(self.graph.input),
            catalog.type_name(self.graph.success),
            self.graph
                .errors
                .iter()
                .map(|type_id| catalog.type_name(*type_id))
                .collect::<Vec<_>>()
                .join(", ")
        ));
        output.push_str("nodes\n");
        for (index, node) in self.graph.nodes.iter().enumerate() {
            let node_id = NodeId(u16::try_from(index + 1).expect("node ID fits"));
            match node {
                NodeKind::Action(action_id) => {
                    let action = catalog
                        .action(*action_id)
                        .expect("verified graph contains known actions");
                    output.push_str(&format!(
                        "  n{} action {} : {} -> {} ! {}\n",
                        node_id.raw(),
                        catalog.action_name(*action_id),
                        catalog.type_name(action.input),
                        catalog.type_name(action.success),
                        catalog.type_name(action.error)
                    ));
                }
                NodeKind::Dispatch(variant) => output.push_str(&format!(
                    "  n{} dispatch {}\n",
                    node_id.raw(),
                    catalog.type_name(*variant)
                )),
                NodeKind::Fork(input, branches) => output.push_str(&format!(
                    "  n{} fork {} branches={}\n",
                    node_id.raw(),
                    catalog.type_name(*input),
                    branches
                )),
                NodeKind::Join(types) => output.push_str(&format!(
                    "  n{} join [{}]\n",
                    node_id.raw(),
                    types
                        .iter()
                        .flatten()
                        .map(|type_id| catalog.type_name(*type_id))
                        .collect::<Vec<_>>()
                        .join(", ")
                )),
                NodeKind::Terminal(kind) => output.push_str(&format!(
                    "  n{} terminal {}\n",
                    node_id.raw(),
                    terminal_name(*kind)
                )),
            }
        }
        output.push_str("edges\n");
        for edge in &self.graph.edges {
            output.push_str(&format!(
                "  {} -> {}{}{}\n",
                source_name(edge.source),
                target_name(edge.target),
                edge_type_text(edge, &self.graph, catalog),
                edge.presentation.map_or_else(String::new, |type_id| {
                    format!(" as {}", catalog.type_name(type_id))
                })
            ));
        }
        output
    }

    pub fn to_dot(&self, catalog: &Catalog) -> String {
        let mut output = format!(
            "digraph \"{}\" {{\n  rankdir=LR;\n",
            dot_escape(&self.graph.name)
        );
        output.push_str(&format!(
            "  input [shape=oval,label=\"input\\n{}\"];\n",
            dot_escape(&catalog.type_name(self.graph.input))
        ));
        for (index, node) in self.graph.nodes.iter().enumerate() {
            let node_id = NodeId(u16::try_from(index + 1).expect("node ID fits"));
            match node {
                NodeKind::Action(action_id) => output.push_str(&format!(
                    "  n{} [shape=box,label=\"{}\"];\n",
                    node_id.raw(),
                    dot_escape(&catalog.action_name(*action_id))
                )),
                NodeKind::Dispatch(variant) => output.push_str(&format!(
                    "  n{} [shape=diamond,label=\"match\\n{}\"];\n",
                    node_id.raw(),
                    dot_escape(&catalog.type_name(*variant))
                )),
                NodeKind::Fork(_, branches) => output.push_str(&format!(
                    "  n{} [shape=triangle,label=\"all\\n{} branches\"];\n",
                    node_id.raw(),
                    branches
                )),
                NodeKind::Join(_) => output.push_str(&format!(
                    "  n{} [shape=invtriangle,label=\"join\"];\n",
                    node_id.raw()
                )),
                NodeKind::Terminal(kind) => output.push_str(&format!(
                    "  n{} [shape=doublecircle,label=\"{}\"];\n",
                    node_id.raw(),
                    terminal_name(*kind)
                )),
            }
        }
        for edge in &self.graph.edges {
            let label = edge.presentation.map_or_else(
                || {
                    source_value_type(edge.source, &self.graph, catalog)
                        .expect("verified edge has a valid source")
                        .map_or_else(
                            || match edge.source {
                                EdgeSource::ActionNotSent(_) => "not sent".to_owned(),
                                EdgeSource::ActionUnknown(_) => "delivery unknown".to_owned(),
                                _ => unreachable!("only delivery outcomes are untyped"),
                            },
                            |type_id| catalog.type_name(type_id),
                        )
                },
                |type_id| format!("as {}", catalog.type_name(type_id)),
            );
            output.push_str(&format!(
                "  {} -> {} [label=\"{}\"];\n",
                dot_source(edge.source),
                dot_target(edge.target),
                dot_escape(&label)
            ));
        }
        output.push_str("}\n");
        output
    }
}

#[derive(Clone, Debug)]
pub(crate) enum ImageNode {
    Action(ActionId),
    Dispatch(TypeId),
    Fork(TypeId, u8),
    Join([Option<TypeId>; MAX_ALL_BRANCHES]),
    Terminal(TerminalKind),
}

#[derive(Clone, Debug)]
pub(crate) struct ImageEdge {
    pub source: EdgeSource,
    pub target: EdgeTarget,
    pub source_type: Option<TypeId>,
    pub target_type: Option<TypeId>,
    pub presentation: Option<TypeId>,
}

#[derive(Clone, Debug)]
pub(crate) struct GraphImageView {
    pub name: String,
    pub input: TypeId,
    pub success: TypeId,
    pub errors: Vec<TypeId>,
    pub nodes: Vec<ImageNode>,
    pub edges: Vec<ImageEdge>,
    pub deadlines: Vec<DeadlineRegion>,
    pub each_regions: Vec<EachRegion>,
}

fn verify(graph: &GraphBuilder, catalog: &Catalog) -> Result<(), GraphError> {
    if graph.nodes.len() > MAX_GRAPH_NODES || graph.edges.len() > MAX_GRAPH_EDGES {
        return Err(GraphError::graph(
            GraphErrorCode::LimitExceeded,
            format!(
                "graph has {} nodes and {} edges; limits are {MAX_GRAPH_NODES} and {MAX_GRAPH_EDGES}",
                graph.nodes.len(),
                graph.edges.len()
            ),
        ));
    }
    if graph.deadlines.len() > 8 {
        return Err(GraphError::graph(
            GraphErrorCode::LimitExceeded,
            "graph contains more than 8 deadline regions",
        ));
    }
    for (index, region) in graph.deadlines.iter().enumerate() {
        let region_id = u16::try_from(index + 1).expect("deadline region bound fits u16");
        let root = region.first_node == 0 && region.node_count == 0;
        if region.duration_ms == 0
            || (region.parent != 0 && region.parent >= region_id)
            || (root && (index != 0 || region.parent != 0))
            || (!root
                && (region.node_count == 0
                    || usize::from(region.first_node) + usize::from(region.node_count) - 1
                        > graph.nodes.len()))
        {
            return Err(GraphError::graph(
                GraphErrorCode::InvalidRoute,
                format!("deadline region {region_id} has invalid bounds or parent"),
            ));
        }
    }
    if graph.each_regions.len() > 8 {
        return Err(GraphError::graph(
            GraphErrorCode::LimitExceeded,
            "graph contains more than 8 Each regions",
        ));
    }
    let mut templates = BTreeSet::new();
    for (index, region) in graph.each_regions.iter().enumerate() {
        let region_id = index + 1;
        let source_list = catalog.list_element(region.source_list);
        let collected = catalog.list_element(region.collected);
        let template = node_at(graph, region.template).and_then(|node| match node {
            NodeKind::Action(action) => catalog.action(action),
            _ => None,
        });
        if region.bound == 0
            || region.concurrency == 0
            || u16::from(region.concurrency) > region.bound
            || source_list != Some((region.item_input, region.bound))
            || collected.is_none_or(|(element, maximum)| {
                element != region.item_output || maximum < region.bound
            })
            || template.is_none_or(|action| action.success != region.item_output)
            || !templates.insert(region.template)
        {
            return Err(GraphError::graph(
                GraphErrorCode::InvalidRoute,
                format!("Each region {region_id} has an invalid bound, type, or template"),
            ));
        }
    }
    for type_id in std::iter::once(graph.input)
        .chain(std::iter::once(graph.success))
        .chain(graph.errors.iter().copied())
    {
        if catalog.type_def(type_id).is_none() {
            return Err(GraphError::graph(
                GraphErrorCode::UnknownType,
                format!("workflow references unknown type ID {}", type_id.raw()),
            ));
        }
    }
    let unique_errors: BTreeSet<_> = graph.errors.iter().copied().collect();
    if unique_errors.len() != graph.errors.len() {
        return Err(GraphError::graph(
            GraphErrorCode::DuplicateErrorType,
            "workflow error set contains a duplicate nominal type",
        ));
    }
    for (index, node) in graph.nodes.iter().enumerate() {
        match node {
            NodeKind::Action(action_id) if catalog.action(*action_id).is_none() => {
                return Err(GraphError::node(
                    GraphErrorCode::UnknownAction,
                    node_id(index),
                    format!("unknown Action ID {}", action_id.raw()),
                ));
            }
            NodeKind::Dispatch(type_id) if catalog.variant_cases(*type_id).is_none() => {
                return Err(GraphError::node(
                    GraphErrorCode::UnknownType,
                    node_id(index),
                    format!(
                        "{} is not a dispatchable named variant",
                        catalog.type_name(*type_id)
                    ),
                ));
            }
            NodeKind::Fork(type_id, branches)
                if catalog.type_def(*type_id).is_none()
                    || *branches < 2
                    || usize::from(*branches) > MAX_ALL_BRANCHES =>
            {
                return Err(GraphError::node(
                    GraphErrorCode::InvalidPortCardinality,
                    node_id(index),
                    format!(
                        "Fork must reference a known type and have 2..={MAX_ALL_BRANCHES} branches"
                    ),
                ));
            }
            NodeKind::Join(types)
                if !(2..=MAX_ALL_BRANCHES).contains(&types.iter().flatten().count())
                    || types
                        .iter()
                        .flatten()
                        .any(|type_id| catalog.type_def(*type_id).is_none()) =>
            {
                return Err(GraphError::node(
                    GraphErrorCode::InvalidPortCardinality,
                    node_id(index),
                    format!("Join must contain 2..={MAX_ALL_BRANCHES} known branch types"),
                ));
            }
            _ => {}
        }
    }
    verify_terminals(graph)?;
    verify_edges(graph, catalog)?;
    verify_acyclic(graph)?;
    verify_cardinality(graph, catalog)?;
    verify_reachability(graph)?;
    Ok(())
}

fn verify_terminals(graph: &GraphBuilder) -> Result<(), GraphError> {
    for terminal in [
        TerminalKind::Success,
        TerminalKind::KnownFailure,
        TerminalKind::NotSent,
        TerminalKind::Unknown,
    ] {
        let count = graph
            .nodes
            .iter()
            .filter(|node| matches!(node, NodeKind::Terminal(kind) if *kind == terminal))
            .count();
        if count == 0 {
            return Err(GraphError::graph(
                GraphErrorCode::MissingTerminal,
                format!("missing {} terminal", terminal_name(terminal)),
            ));
        }
        if count > 1 {
            return Err(GraphError::graph(
                GraphErrorCode::DuplicateTerminal,
                format!("duplicate {} terminal", terminal_name(terminal)),
            ));
        }
    }
    Ok(())
}

fn verify_edges(graph: &GraphBuilder, catalog: &Catalog) -> Result<(), GraphError> {
    let mut unique = BTreeSet::new();
    for (index, edge) in graph.edges.iter().enumerate() {
        if !unique.insert((edge.source, edge.target)) {
            return Err(GraphError::edge(
                GraphErrorCode::DuplicateEdge,
                index,
                format!(
                    "duplicate edge {} -> {}",
                    source_name(edge.source),
                    target_name(edge.target)
                ),
            ));
        }
        let source_type = source_value_type(edge.source, graph, catalog)?;
        let target = node_for_target(edge.target, graph).ok_or_else(|| {
            GraphError::edge(
                GraphErrorCode::InvalidEndpoint,
                index,
                format!("invalid target {}", target_name(edge.target)),
            )
        })?;
        verify_route(index, edge.source, edge.target, target)?;
        if source_type.is_none() {
            if edge.presentation.is_some() {
                return Err(GraphError::edge(
                    GraphErrorCode::InvalidPresentation,
                    index,
                    "delivery-outcome edge cannot carry a nominal presentation",
                ));
            }
            continue;
        }
        let source_type = source_type.expect("typed source checked above");
        let effective_type = if let Some(presentation) = edge.presentation {
            if catalog.type_def(presentation).is_none() {
                return Err(GraphError::edge(
                    GraphErrorCode::UnknownType,
                    index,
                    format!(
                        "presentation references unknown type ID {}",
                        presentation.raw()
                    ),
                ));
            }
            catalog
                .representation_compatible(source_type, presentation)
                .map_err(|path| {
                    GraphError::edge(
                        GraphErrorCode::RepresentationMismatch,
                        index,
                        format!(
                            "cannot present {} as {}: {path}",
                            catalog.type_name(source_type),
                            catalog.type_name(presentation)
                        ),
                    )
                })?;
            presentation
        } else {
            source_type
        };
        let expected = target_expected_type(edge.target, target, graph, catalog);
        match expected {
            TargetExpectation::One(type_id) if effective_type != type_id => {
                return Err(GraphError::edge(
                    GraphErrorCode::NominalMismatch,
                    index,
                    format!(
                        "{} supplies {}, but {} expects {}",
                        source_name(edge.source),
                        catalog.type_name(effective_type),
                        target_name(edge.target),
                        catalog.type_name(type_id)
                    ),
                ));
            }
            TargetExpectation::One(_) => {}
            TargetExpectation::Any(types) if !types.contains(&effective_type) => {
                return Err(GraphError::edge(
                    GraphErrorCode::NominalMismatch,
                    index,
                    format!(
                        "{} is not one of the workflow's declared error types",
                        catalog.type_name(effective_type)
                    ),
                ));
            }
            TargetExpectation::Any(_) => {}
            TargetExpectation::None => {
                return Err(GraphError::edge(
                    GraphErrorCode::InvalidRoute,
                    index,
                    "typed edge targets an untyped delivery-outcome terminal",
                ));
            }
        }
    }
    Ok(())
}

fn verify_route(
    edge_index: usize,
    source: EdgeSource,
    target: EdgeTarget,
    target_kind: NodeKind,
) -> Result<(), GraphError> {
    let valid = matches!(
        (source, target, target_kind),
        (
            EdgeSource::WorkflowInput,
            EdgeTarget::ActionInput(_),
            NodeKind::Action(_)
        ) | (
            EdgeSource::WorkflowInput,
            EdgeTarget::DispatchInput(_),
            NodeKind::Dispatch(_)
        ) | (
            EdgeSource::WorkflowInput,
            EdgeTarget::ForkInput(_),
            NodeKind::Fork(_, _)
        ) | (
            EdgeSource::ActionSuccess(_),
            EdgeTarget::ActionInput(_),
            NodeKind::Action(_)
        ) | (
            EdgeSource::ActionSuccess(_),
            EdgeTarget::DispatchInput(_),
            NodeKind::Dispatch(_)
        ) | (
            EdgeSource::ActionSuccess(_),
            EdgeTarget::ForkInput(_),
            NodeKind::Fork(_, _)
        ) | (
            EdgeSource::DispatchCase(_, _),
            EdgeTarget::ForkInput(_),
            NodeKind::Fork(_, _)
        ) | (
            EdgeSource::JoinField(_, _),
            EdgeTarget::ForkInput(_),
            NodeKind::Fork(_, _)
        ) | (
            EdgeSource::ForkBranch(_, _),
            EdgeTarget::ActionInput(_),
            NodeKind::Action(_)
        ) | (
            EdgeSource::ActionSuccess(_),
            EdgeTarget::JoinInput(_, _),
            NodeKind::Join(_)
        ) | (
            EdgeSource::JoinField(_, _),
            EdgeTarget::ActionInput(_),
            NodeKind::Action(_)
        ) | (
            EdgeSource::EachItem(_),
            EdgeTarget::ActionInput(_),
            NodeKind::Action(_)
        ) | (
            EdgeSource::ActionSuccess(_),
            EdgeTarget::EachInput(_),
            NodeKind::Action(_)
        ) | (
            EdgeSource::WorkflowInput,
            EdgeTarget::EachInput(_),
            NodeKind::Action(_)
        ) | (
            EdgeSource::ActionSuccess(_),
            EdgeTarget::EachCollect(_),
            NodeKind::Action(_)
        ) | (
            EdgeSource::EachOutput(_),
            EdgeTarget::ActionInput(_),
            NodeKind::Action(_)
        ) | (
            EdgeSource::ActionError(_),
            EdgeTarget::ActionInput(_),
            NodeKind::Action(_)
        ) | (
            EdgeSource::DispatchCase(_, _),
            EdgeTarget::ActionInput(_),
            NodeKind::Action(_)
        ) | (
            EdgeSource::JoinField(_, _),
            EdgeTarget::Terminal(_),
            NodeKind::Terminal(TerminalKind::Success)
        ) | (
            EdgeSource::EachOutput(_),
            EdgeTarget::Terminal(_),
            NodeKind::Terminal(TerminalKind::Success)
        ) | (
            EdgeSource::ActionSuccess(_),
            EdgeTarget::Terminal(_),
            NodeKind::Terminal(TerminalKind::Success),
        ) | (
            EdgeSource::ActionError(_),
            EdgeTarget::Terminal(_),
            NodeKind::Terminal(TerminalKind::KnownFailure),
        ) | (
            EdgeSource::ActionNotSent(_),
            EdgeTarget::Terminal(_),
            NodeKind::Terminal(TerminalKind::NotSent),
        ) | (
            EdgeSource::ActionUnknown(_),
            EdgeTarget::Terminal(_),
            NodeKind::Terminal(TerminalKind::Unknown),
        )
    );
    if valid {
        Ok(())
    } else {
        Err(GraphError::edge(
            GraphErrorCode::InvalidRoute,
            edge_index,
            format!(
                "{} cannot target {}",
                source_name(source),
                target_name(target)
            ),
        ))
    }
}

fn verify_acyclic(graph: &GraphBuilder) -> Result<(), GraphError> {
    let mut incoming = vec![0usize; graph.nodes.len()];
    let mut outgoing: Vec<Vec<usize>> = vec![Vec::new(); graph.nodes.len()];
    for edge in &graph.edges {
        let Some(source) = source_node(edge.source, graph) else {
            continue;
        };
        let Some(target) = target_node(edge.target, graph) else {
            continue;
        };
        let Some(source_index) = index_of(source, graph.nodes.len()) else {
            continue;
        };
        let Some(target_index) = index_of(target, graph.nodes.len()) else {
            continue;
        };
        outgoing[source_index].push(target_index);
        incoming[target_index] += 1;
    }
    let mut ready: VecDeque<_> = incoming
        .iter()
        .enumerate()
        .filter_map(|(index, count)| (*count == 0).then_some(index))
        .collect();
    let mut visited = 0usize;
    while let Some(index) = ready.pop_front() {
        visited += 1;
        for target in &outgoing[index] {
            incoming[*target] -= 1;
            if incoming[*target] == 0 {
                ready.push_back(*target);
            }
        }
    }
    if visited != graph.nodes.len() {
        return Err(GraphError::graph(
            GraphErrorCode::Cycle,
            "graph contains a cycle",
        ));
    }
    Ok(())
}

fn verify_cardinality(graph: &GraphBuilder, catalog: &Catalog) -> Result<(), GraphError> {
    let workflow_inputs = graph
        .edges
        .iter()
        .filter(|edge| edge.source == EdgeSource::WorkflowInput)
        .count();
    if workflow_inputs != 1 {
        return Err(GraphError::graph(
            GraphErrorCode::InvalidPortCardinality,
            format!("workflow input has {workflow_inputs} outgoing edges; expected 1"),
        ));
    }
    for (index, node) in graph.nodes.iter().enumerate() {
        let id = node_id(index);
        match node {
            NodeKind::Action(_) => {
                let incoming = graph
                    .edges
                    .iter()
                    .filter(|edge| edge.target == EdgeTarget::ActionInput(id))
                    .count();
                if incoming != 1 {
                    return Err(GraphError::node(
                        GraphErrorCode::InvalidPortCardinality,
                        id,
                        format!("Action input has {incoming} incoming edges; expected 1"),
                    ));
                }
                for (label, source) in [
                    ("success", EdgeSource::ActionSuccess(id)),
                    ("error", EdgeSource::ActionError(id)),
                    ("not-sent", EdgeSource::ActionNotSent(id)),
                    ("unknown", EdgeSource::ActionUnknown(id)),
                ] {
                    let outgoing = graph
                        .edges
                        .iter()
                        .filter(|edge| edge.source == source)
                        .count();
                    if outgoing != 1 {
                        return Err(GraphError::node(
                            GraphErrorCode::InvalidPortCardinality,
                            id,
                            format!(
                                "Action {label} port has {outgoing} outgoing edges; expected 1"
                            ),
                        ));
                    }
                }
            }
            NodeKind::Dispatch(variant) => {
                let incoming = graph
                    .edges
                    .iter()
                    .filter(|edge| edge.target == EdgeTarget::DispatchInput(id))
                    .count();
                if incoming != 1 {
                    return Err(GraphError::node(
                        GraphErrorCode::InvalidPortCardinality,
                        id,
                        format!("Dispatch input has {incoming} incoming edges; expected 1"),
                    ));
                }
                let case_count = catalog
                    .variant_cases(*variant)
                    .expect("verified dispatch variant")
                    .len();
                for tag in 0..case_count {
                    let source = EdgeSource::DispatchCase(
                        id,
                        u8::try_from(tag).expect("graph edge bound fits tag"),
                    );
                    let outgoing = graph
                        .edges
                        .iter()
                        .filter(|edge| edge.source == source)
                        .count();
                    if outgoing != 1 {
                        return Err(GraphError::node(
                            GraphErrorCode::InvalidPortCardinality,
                            id,
                            format!(
                                "Dispatch case {tag} has {outgoing} outgoing edges; expected 1"
                            ),
                        ));
                    }
                }
            }
            NodeKind::Fork(_, branches) => {
                let incoming = graph
                    .edges
                    .iter()
                    .filter(|edge| edge.target == EdgeTarget::ForkInput(id))
                    .count();
                if incoming != 1 {
                    return Err(GraphError::node(
                        GraphErrorCode::InvalidPortCardinality,
                        id,
                        format!("Fork input has {incoming} incoming edges; expected 1"),
                    ));
                }
                for branch in 0..*branches {
                    let outgoing = graph
                        .edges
                        .iter()
                        .filter(|edge| edge.source == EdgeSource::ForkBranch(id, branch))
                        .count();
                    if outgoing != 1 {
                        return Err(GraphError::node(
                            GraphErrorCode::InvalidPortCardinality,
                            id,
                            format!(
                                "Fork branch {branch} has {outgoing} outgoing edges; expected 1"
                            ),
                        ));
                    }
                }
            }
            NodeKind::Join(types) => {
                let branch_count = types.iter().flatten().count();
                let mut outputs = 0usize;
                for branch in 0..branch_count {
                    let tag = u8::try_from(branch).expect("join branch bound fits u8");
                    let incoming = graph
                        .edges
                        .iter()
                        .filter(|edge| edge.target == EdgeTarget::JoinInput(id, tag))
                        .count();
                    let outgoing = graph
                        .edges
                        .iter()
                        .filter(|edge| edge.source == EdgeSource::JoinField(id, tag))
                        .count();
                    if incoming != 1 || outgoing > 1 {
                        return Err(GraphError::node(
                            GraphErrorCode::InvalidPortCardinality,
                            id,
                            format!(
                                "Join field {branch} has {incoming} inputs and {outgoing} outputs; expected 1 and at most 1"
                            ),
                        ));
                    }
                    outputs += outgoing;
                }
                if outputs == 0 {
                    return Err(GraphError::node(
                        GraphErrorCode::InvalidPortCardinality,
                        id,
                        "Join must expose at least one used result field",
                    ));
                }
            }
            NodeKind::Terminal(_) => {}
        }
    }
    for (index, region) in graph.each_regions.iter().enumerate() {
        let id = u8::try_from(index + 1).expect("Each region bound fits u8");
        for (label, count) in [
            (
                "input",
                graph
                    .edges
                    .iter()
                    .filter(|edge| edge.target == EdgeTarget::EachInput(id))
                    .count(),
            ),
            (
                "item",
                graph
                    .edges
                    .iter()
                    .filter(|edge| edge.source == EdgeSource::EachItem(id))
                    .count(),
            ),
            (
                "collect",
                graph
                    .edges
                    .iter()
                    .filter(|edge| edge.target == EdgeTarget::EachCollect(id))
                    .count(),
            ),
            (
                "output",
                graph
                    .edges
                    .iter()
                    .filter(|edge| edge.source == EdgeSource::EachOutput(id))
                    .count(),
            ),
        ] {
            if count != 1 {
                return Err(GraphError::node(
                    GraphErrorCode::InvalidPortCardinality,
                    region.template,
                    format!("Each region {id} {label} port has {count} edges; expected 1"),
                ));
            }
        }
    }
    Ok(())
}

fn verify_reachability(graph: &GraphBuilder) -> Result<(), GraphError> {
    let mut reached = BTreeSet::new();
    let mut queue = VecDeque::new();
    for edge in &graph.edges {
        if edge.source == EdgeSource::WorkflowInput
            && let Some(target) = target_node(edge.target, graph)
        {
            queue.push_back(target);
        }
    }
    while let Some(node) = queue.pop_front() {
        if !reached.insert(node) {
            continue;
        }
        for edge in &graph.edges {
            if source_node(edge.source, graph) == Some(node)
                && let Some(target) = target_node(edge.target, graph)
            {
                queue.push_back(target);
            }
        }
    }
    for index in 0..graph.nodes.len() {
        let id = node_id(index);
        if !reached.contains(&id) {
            return Err(GraphError::node(
                GraphErrorCode::UnreachableNode,
                id,
                "node is unreachable from the workflow input",
            ));
        }
    }
    Ok(())
}

enum TargetExpectation<'a> {
    One(TypeId),
    Any(&'a [TypeId]),
    None,
}

fn target_expected_type<'a>(
    target_port: EdgeTarget,
    target: NodeKind,
    graph: &'a GraphBuilder,
    catalog: &Catalog,
) -> TargetExpectation<'a> {
    match target_port {
        EdgeTarget::EachInput(region) => {
            return each_region(graph, region).map_or(TargetExpectation::None, |item| {
                TargetExpectation::One(item.source_list)
            });
        }
        EdgeTarget::EachCollect(region) => {
            return each_region(graph, region).map_or(TargetExpectation::None, |item| {
                TargetExpectation::One(item.item_output)
            });
        }
        _ => {}
    }
    match target {
        NodeKind::Action(action_id) => TargetExpectation::One(
            catalog
                .action(action_id)
                .expect("verified action reference")
                .input,
        ),
        NodeKind::Dispatch(variant) => TargetExpectation::One(variant),
        NodeKind::Fork(input, _) => TargetExpectation::One(input),
        NodeKind::Join(types) => match target_port {
            EdgeTarget::JoinInput(_, tag) => types
                .get(usize::from(tag))
                .and_then(|type_id| *type_id)
                .map_or(TargetExpectation::None, TargetExpectation::One),
            _ => TargetExpectation::None,
        },
        NodeKind::Terminal(TerminalKind::Success) => TargetExpectation::One(graph.success),
        NodeKind::Terminal(TerminalKind::KnownFailure) => TargetExpectation::Any(&graph.errors),
        NodeKind::Terminal(TerminalKind::NotSent) => TargetExpectation::None,
        NodeKind::Terminal(TerminalKind::Unknown) => TargetExpectation::None,
    }
}

fn source_value_type(
    source: EdgeSource,
    graph: &GraphBuilder,
    catalog: &Catalog,
) -> Result<Option<TypeId>, GraphError> {
    match source {
        EdgeSource::WorkflowInput => Ok(Some(graph.input)),
        EdgeSource::ActionSuccess(node) => {
            action_for_node(node, graph, catalog).map(|action| Some(action.success))
        }
        EdgeSource::ActionError(node) => {
            action_for_node(node, graph, catalog).map(|action| Some(action.error))
        }
        EdgeSource::ActionNotSent(node) => action_for_node(node, graph, catalog).map(|_| None),
        EdgeSource::ActionUnknown(node) => action_for_node(node, graph, catalog).map(|_| None),
        EdgeSource::DispatchCase(node, tag) => {
            dispatch_case_type(node, tag, graph, catalog).map(Some)
        }
        EdgeSource::ForkBranch(node, tag) => match node_at(graph, node) {
            Some(NodeKind::Fork(input, branches)) if tag < branches => Ok(Some(input)),
            _ => Err(GraphError::node(
                GraphErrorCode::InvalidEndpoint,
                node,
                format!("Fork has no branch {tag}"),
            )),
        },
        EdgeSource::JoinField(node, tag) => match node_at(graph, node) {
            Some(NodeKind::Join(types)) => types
                .get(usize::from(tag))
                .and_then(|type_id| *type_id)
                .map(Some)
                .ok_or_else(|| {
                    GraphError::node(
                        GraphErrorCode::InvalidEndpoint,
                        node,
                        format!("Join has no field {tag}"),
                    )
                }),
            _ => Err(GraphError::node(
                GraphErrorCode::InvalidEndpoint,
                node,
                "edge source does not reference a Join node",
            )),
        },
        EdgeSource::EachItem(region) => each_region(graph, region)
            .map(|item| Some(item.item_input))
            .ok_or_else(|| {
                GraphError::graph(
                    GraphErrorCode::InvalidEndpoint,
                    format!("unknown Each region {region}"),
                )
            }),
        EdgeSource::EachOutput(region) => each_region(graph, region)
            .map(|item| Some(item.collected))
            .ok_or_else(|| {
                GraphError::graph(
                    GraphErrorCode::InvalidEndpoint,
                    format!("unknown Each region {region}"),
                )
            }),
    }
}

fn each_region(graph: &GraphBuilder, region: u8) -> Option<&EachRegion> {
    region
        .checked_sub(1)
        .map(usize::from)
        .and_then(|index| graph.each_regions.get(index))
}

fn action_for_node<'a>(
    node: NodeId,
    graph: &GraphBuilder,
    catalog: &'a Catalog,
) -> Result<&'a crate::catalog::ActionDef, GraphError> {
    match node_at(graph, node) {
        Some(NodeKind::Action(action_id)) => catalog.action(action_id).ok_or_else(|| {
            GraphError::node(
                GraphErrorCode::UnknownAction,
                node,
                format!("unknown Action ID {}", action_id.raw()),
            )
        }),
        _ => Err(GraphError::node(
            GraphErrorCode::InvalidEndpoint,
            node,
            "edge source does not reference an Action node",
        )),
    }
}

fn dispatch_case_type(
    node: NodeId,
    tag: u8,
    graph: &GraphBuilder,
    catalog: &Catalog,
) -> Result<TypeId, GraphError> {
    let Some(NodeKind::Dispatch(variant)) = node_at(graph, node) else {
        return Err(GraphError::node(
            GraphErrorCode::InvalidEndpoint,
            node,
            "edge source does not reference a Dispatch node",
        ));
    };
    catalog
        .variant_cases(variant)
        .and_then(|cases| cases.values().nth(usize::from(tag)).copied())
        .ok_or_else(|| {
            GraphError::node(
                GraphErrorCode::InvalidEndpoint,
                node,
                format!("Dispatch has no case tag {tag}"),
            )
        })
}

fn node_for_target(target: EdgeTarget, graph: &GraphBuilder) -> Option<NodeKind> {
    match target {
        EdgeTarget::ActionInput(node)
        | EdgeTarget::DispatchInput(node)
        | EdgeTarget::ForkInput(node)
        | EdgeTarget::JoinInput(node, _) => node_at(graph, node),
        EdgeTarget::EachInput(region) | EdgeTarget::EachCollect(region) => {
            each_region(graph, region).and_then(|item| node_at(graph, item.template))
        }
        EdgeTarget::Terminal(node) => node_at(graph, node),
    }
}

fn node_at(graph: &GraphBuilder, node: NodeId) -> Option<NodeKind> {
    index_of(node, graph.nodes.len()).map(|index| graph.nodes[index])
}

fn index_of(node: NodeId, length: usize) -> Option<usize> {
    node.raw()
        .checked_sub(1)
        .map(usize::from)
        .filter(|index| *index < length)
}

fn node_id(index: usize) -> NodeId {
    NodeId(u16::try_from(index + 1).expect("node index fits in u16"))
}

fn source_node(source: EdgeSource, graph: &GraphBuilder) -> Option<NodeId> {
    match source {
        EdgeSource::WorkflowInput => None,
        EdgeSource::ActionSuccess(node)
        | EdgeSource::ActionError(node)
        | EdgeSource::ActionNotSent(node)
        | EdgeSource::ActionUnknown(node)
        | EdgeSource::DispatchCase(node, _)
        | EdgeSource::ForkBranch(node, _)
        | EdgeSource::JoinField(node, _) => Some(node),
        EdgeSource::EachItem(_) => None,
        EdgeSource::EachOutput(region) => each_region(graph, region).map(|item| item.template),
    }
}

fn target_node(target: EdgeTarget, graph: &GraphBuilder) -> Option<NodeId> {
    match target {
        EdgeTarget::ActionInput(node)
        | EdgeTarget::DispatchInput(node)
        | EdgeTarget::ForkInput(node)
        | EdgeTarget::JoinInput(node, _)
        | EdgeTarget::Terminal(node) => Some(node),
        EdgeTarget::EachInput(region) => each_region(graph, region).map(|item| item.template),
        EdgeTarget::EachCollect(_) => None,
    }
}

fn graph_types<'a>(
    graph: &'a GraphBuilder,
    catalog: &'a Catalog,
) -> impl Iterator<Item = TypeId> + 'a {
    std::iter::once(graph.input)
        .chain(std::iter::once(graph.success))
        .chain(graph.errors.iter().copied())
        .chain(graph.nodes.iter().filter_map(|node| match node {
            NodeKind::Action(action_id) => catalog.action(*action_id).map(|action| action.input),
            NodeKind::Dispatch(variant) => Some(*variant),
            NodeKind::Fork(input, _) => Some(*input),
            NodeKind::Join(_) => None,
            NodeKind::Terminal(_) => None,
        }))
        .chain(graph.nodes.iter().filter_map(|node| match node {
            NodeKind::Action(action_id) => catalog.action(*action_id).map(|action| action.success),
            NodeKind::Dispatch(_) | NodeKind::Fork(_, _) | NodeKind::Join(_) => None,
            NodeKind::Terminal(_) => None,
        }))
        .chain(graph.nodes.iter().filter_map(|node| match node {
            NodeKind::Action(action_id) => catalog.action(*action_id).map(|action| action.error),
            NodeKind::Dispatch(_) | NodeKind::Fork(_, _) | NodeKind::Join(_) => None,
            NodeKind::Terminal(_) => None,
        }))
        .chain(graph.nodes.iter().flat_map(|node| {
            match node {
                NodeKind::Dispatch(variant) => catalog
                    .variant_cases(*variant)
                    .into_iter()
                    .flat_map(|cases| cases.values().copied())
                    .collect::<Vec<_>>(),
                NodeKind::Join(types) => types.iter().flatten().copied().collect(),
                _ => Vec::new(),
            }
        }))
        .chain(graph.each_regions.iter().flat_map(|region| {
            [
                region.source_list,
                region.item_input,
                region.item_output,
                region.collected,
            ]
        }))
}

fn edge_type_text(edge: &Edge, graph: &GraphBuilder, catalog: &Catalog) -> String {
    source_value_type(edge.source, graph, catalog)
        .ok()
        .flatten()
        .map_or_else(String::new, |type_id| {
            format!(" : {}", catalog.type_name(type_id))
        })
}

fn terminal_name(kind: TerminalKind) -> &'static str {
    match kind {
        TerminalKind::Success => "success",
        TerminalKind::KnownFailure => "known-failure",
        TerminalKind::NotSent => "not-sent",
        TerminalKind::Unknown => "unknown",
    }
}

fn source_name(source: EdgeSource) -> String {
    match source {
        EdgeSource::WorkflowInput => "workflow.input".to_owned(),
        EdgeSource::ActionSuccess(node) => format!("n{}.success", node.raw()),
        EdgeSource::ActionError(node) => format!("n{}.error", node.raw()),
        EdgeSource::ActionNotSent(node) => format!("n{}.not-sent", node.raw()),
        EdgeSource::ActionUnknown(node) => format!("n{}.unknown", node.raw()),
        EdgeSource::DispatchCase(node, tag) => format!("n{}.case[{tag}]", node.raw()),
        EdgeSource::ForkBranch(node, branch) => {
            format!("n{}.branch[{branch}]", node.raw())
        }
        EdgeSource::JoinField(node, field) => format!("n{}.field[{field}]", node.raw()),
        EdgeSource::EachItem(region) => format!("each[{region}].item"),
        EdgeSource::EachOutput(region) => format!("each[{region}].output"),
    }
}

fn target_name(target: EdgeTarget) -> String {
    match target {
        EdgeTarget::ActionInput(node) => format!("n{}.input", node.raw()),
        EdgeTarget::DispatchInput(node) => format!("n{}.input", node.raw()),
        EdgeTarget::ForkInput(node) => format!("n{}.input", node.raw()),
        EdgeTarget::JoinInput(node, branch) => {
            format!("n{}.branch[{branch}]", node.raw())
        }
        EdgeTarget::EachInput(region) => format!("each[{region}].input"),
        EdgeTarget::EachCollect(region) => format!("each[{region}].collect"),
        EdgeTarget::Terminal(node) => format!("n{}", node.raw()),
    }
}

fn dot_source(source: EdgeSource) -> String {
    match source {
        EdgeSource::WorkflowInput => "input".to_owned(),
        EdgeSource::ActionSuccess(node)
        | EdgeSource::ActionError(node)
        | EdgeSource::ActionNotSent(node)
        | EdgeSource::ActionUnknown(node)
        | EdgeSource::DispatchCase(node, _)
        | EdgeSource::ForkBranch(node, _)
        | EdgeSource::JoinField(node, _) => format!("n{}", node.raw()),
        EdgeSource::EachItem(region) => format!("each{region}_input"),
        EdgeSource::EachOutput(region) => format!("each{region}_output"),
    }
}

fn dot_target(target: EdgeTarget) -> String {
    match target {
        EdgeTarget::ActionInput(node)
        | EdgeTarget::DispatchInput(node)
        | EdgeTarget::ForkInput(node)
        | EdgeTarget::JoinInput(node, _)
        | EdgeTarget::Terminal(node) => {
            format!("n{}", node.raw())
        }
        EdgeTarget::EachInput(region) => format!("each{region}_input"),
        EdgeTarget::EachCollect(region) => format!("each{region}_collect"),
    }
}

fn dot_escape(value: &str) -> String {
    value.replace('\\', "\\\\").replace('"', "\\\"")
}
