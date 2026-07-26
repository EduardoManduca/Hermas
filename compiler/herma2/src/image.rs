use std::collections::{BTreeMap, BTreeSet, VecDeque};
use std::error::Error;
use std::fmt;

use crate::catalog::{Catalog, Representation};
use crate::graph::{
    EdgeSource, EdgeTarget, ImageNode, MAX_GRAPH_EDGES, MAX_GRAPH_NODES, TerminalKind,
    VerifiedGraph,
};

const MAGIC: &[u8; 4] = b"H2GI";
const VERSION: u16 = 1;
const HEADER_SIZE: usize = 80;
const APP_RECORD_SIZE: usize = 36;
const TYPE_RECORD_SIZE: usize = 8;
const NODE_RECORD_SIZE: usize = 8;
const EDGE_RECORD_SIZE: usize = 16;
const REGION_RECORD_SIZE: usize = 16;
const MAX_IMAGE_ERRORS: usize = 256;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ImageErrorCode {
    MissingFingerprint,
    SizeOverflow,
    Truncated,
    BadMagic,
    UnsupportedVersion,
    InvalidHeader,
    InvalidOffset,
    InvalidCount,
    InvalidString,
    InvalidRecord,
    DuplicateRecord,
    InvalidTopology,
    InvalidValue,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ImageError {
    pub code: ImageErrorCode,
    pub offset: Option<usize>,
    pub message: String,
}

impl ImageError {
    fn new(code: ImageErrorCode, offset: Option<usize>, message: impl Into<String>) -> Self {
        Self {
            code,
            offset,
            message: message.into(),
        }
    }
}

impl fmt::Display for ImageError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        if let Some(offset) = self.offset {
            write!(
                formatter,
                "{:?} at byte {offset}: {}",
                self.code, self.message
            )
        } else {
            write!(formatter, "{:?}: {}", self.code, self.message)
        }
    }
}

impl Error for ImageError {}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DecodedImage {
    pub workflow_name: String,
    pub input_type: u16,
    pub success_type: u16,
    pub error_count: usize,
    pub app_count: usize,
    pub type_count: usize,
    pub node_count: usize,
    pub edge_count: usize,
    pub region_count: usize,
}

fn align4(value: usize) -> Result<usize, ImageError> {
    value
        .checked_add(3)
        .map(|item| item & !3)
        .ok_or_else(|| ImageError::new(ImageErrorCode::SizeOverflow, None, "image size overflow"))
}

fn u32_size(value: usize) -> Result<u32, ImageError> {
    u32::try_from(value)
        .map_err(|_| ImageError::new(ImageErrorCode::SizeOverflow, None, "image exceeds 4 GiB"))
}

fn put_u16(output: &mut Vec<u8>, value: u16) {
    output.extend_from_slice(&value.to_le_bytes());
}

fn put_u32(output: &mut Vec<u8>, value: u32) {
    output.extend_from_slice(&value.to_le_bytes());
}

fn pad_to(output: &mut Vec<u8>, offset: usize) {
    output.resize(offset, 0);
}

fn encode_representation(representation: &Representation, output: &mut Vec<u8>) {
    let (kind, children, bound): (u8, Vec<&Representation>, u32) = match representation {
        Representation::Unit => (1, Vec::new(), 0),
        Representation::Integer => (2, Vec::new(), 0),
        Representation::Boolean => (3, Vec::new(), 0),
        Representation::String { maximum } => (4, Vec::new(), *maximum),
        Representation::Bytes { maximum } => (5, Vec::new(), *maximum),
        Representation::Record(fields) => (6, fields.values().collect(), 0),
        Representation::List { element, maximum } => {
            (7, vec![element.as_ref()], u32::from(*maximum))
        }
        Representation::Variant(cases) => (8, cases.values().collect(), 0),
    };
    output.extend_from_slice(&[kind, 0]);
    put_u16(
        output,
        u16::try_from(children.len()).expect("catalog representation child count fits"),
    );
    put_u32(output, bound);
    for child in children {
        encode_representation(child, output);
    }
}

pub fn encode_graph_image(graph: &VerifiedGraph, catalog: &Catalog) -> Result<Vec<u8>, ImageError> {
    let view = graph.image_view(catalog);
    let required_apps = view
        .nodes
        .iter()
        .filter_map(|node| match node {
            ImageNode::Action(action) => catalog.action(*action).map(|item| item.app),
            ImageNode::Dispatch(_) | ImageNode::Fork(_, _) | ImageNode::Join(_) => None,
            ImageNode::Terminal(_) => None,
        })
        .collect::<BTreeSet<_>>();
    let apps = required_apps
        .iter()
        .map(|app_id| {
            let app = catalog
                .app(*app_id)
                .expect("verified Action has a known app");
            app.fingerprint
                .map(|fingerprint| (*app_id, fingerprint))
                .ok_or_else(|| {
                    ImageError::new(
                        ImageErrorCode::MissingFingerprint,
                        None,
                        format!("app `{}` has no compiled contract fingerprint", app.name),
                    )
                })
        })
        .collect::<Result<Vec<_>, _>>()?;
    let types = std::iter::once(view.input)
        .chain(std::iter::once(view.success))
        .chain(view.errors.iter().copied())
        .chain(view.edges.iter().flat_map(|edge| {
            [edge.source_type, edge.target_type, edge.presentation]
                .into_iter()
                .flatten()
        }))
        .collect::<BTreeSet<_>>();
    let mut representations = Vec::new();
    let type_representations = types
        .iter()
        .map(|type_id| {
            let start = representations.len();
            let representation = &catalog
                .type_def(*type_id)
                .expect("verified graph references known types")
                .representation;
            encode_representation(representation, &mut representations);
            (*type_id, start)
        })
        .collect::<Vec<_>>();
    let errors_offset = HEADER_SIZE;
    let apps_offset = align4(
        errors_offset
            .checked_add(view.errors.len() * 2)
            .ok_or_else(|| ImageError::new(ImageErrorCode::SizeOverflow, None, "size overflow"))?,
    )?;
    let types_offset = apps_offset
        .checked_add(apps.len() * APP_RECORD_SIZE)
        .ok_or_else(|| ImageError::new(ImageErrorCode::SizeOverflow, None, "size overflow"))?;
    let nodes_offset = types_offset
        .checked_add(types.len() * TYPE_RECORD_SIZE)
        .ok_or_else(|| ImageError::new(ImageErrorCode::SizeOverflow, None, "size overflow"))?;
    let edges_offset = nodes_offset
        .checked_add(view.nodes.len() * NODE_RECORD_SIZE)
        .ok_or_else(|| ImageError::new(ImageErrorCode::SizeOverflow, None, "size overflow"))?;
    let regions_offset = edges_offset
        .checked_add(view.edges.len() * EDGE_RECORD_SIZE)
        .ok_or_else(|| ImageError::new(ImageErrorCode::SizeOverflow, None, "size overflow"))?;
    let region_count = view.deadlines.len() + view.each_regions.len();
    let representations_offset = regions_offset
        .checked_add(region_count * REGION_RECORD_SIZE)
        .ok_or_else(|| ImageError::new(ImageErrorCode::SizeOverflow, None, "size overflow"))?;
    let strings_offset = representations_offset
        .checked_add(representations.len())
        .ok_or_else(|| ImageError::new(ImageErrorCode::SizeOverflow, None, "size overflow"))?;
    let total_size = strings_offset
        .checked_add(view.name.len())
        .ok_or_else(|| ImageError::new(ImageErrorCode::SizeOverflow, None, "size overflow"))?;
    let mut output = Vec::with_capacity(total_size);
    output.extend_from_slice(MAGIC);
    put_u16(&mut output, VERSION);
    put_u16(&mut output, HEADER_SIZE as u16);
    put_u32(&mut output, u32_size(total_size)?);
    put_u32(&mut output, 0);
    put_u32(&mut output, u32_size(strings_offset)?);
    put_u16(
        &mut output,
        u16::try_from(view.name.len()).map_err(|_| {
            ImageError::new(
                ImageErrorCode::SizeOverflow,
                None,
                "workflow name is too long",
            )
        })?,
    );
    put_u16(&mut output, view.input.raw());
    put_u16(&mut output, view.success.raw());
    put_u16(&mut output, view.errors.len() as u16);
    put_u16(&mut output, apps.len() as u16);
    put_u16(&mut output, view.nodes.len() as u16);
    put_u16(&mut output, view.edges.len() as u16);
    put_u16(&mut output, types.len() as u16);
    put_u32(&mut output, u32_size(errors_offset)?);
    put_u32(&mut output, u32_size(apps_offset)?);
    put_u32(&mut output, u32_size(types_offset)?);
    put_u32(&mut output, u32_size(nodes_offset)?);
    put_u32(&mut output, u32_size(edges_offset)?);
    put_u32(&mut output, u32_size(representations_offset)?);
    put_u32(&mut output, u32_size(strings_offset)?);
    put_u32(&mut output, u32_size(representations.len())?);
    put_u16(&mut output, region_count as u16);
    put_u16(&mut output, 0);
    put_u32(&mut output, u32_size(regions_offset)?);
    put_u32(&mut output, 0);
    debug_assert_eq!(output.len(), HEADER_SIZE);
    for error in &view.errors {
        put_u16(&mut output, error.raw());
    }
    pad_to(&mut output, apps_offset);
    for (app, fingerprint) in &apps {
        put_u16(&mut output, app.raw());
        put_u16(&mut output, 0);
        output.extend_from_slice(fingerprint);
    }
    for (type_id, relative_offset) in &type_representations {
        put_u16(&mut output, type_id.raw());
        put_u16(&mut output, 0);
        put_u32(
            &mut output,
            u32_size(representations_offset + relative_offset)?,
        );
    }
    for node in &view.nodes {
        match node {
            ImageNode::Action(action_id) => {
                let action = catalog.action(*action_id).expect("verified Action exists");
                output.extend_from_slice(&[1, 0]);
                put_u16(&mut output, action_id.raw());
                put_u16(&mut output, action.app.raw());
                put_u16(&mut output, 0);
            }
            ImageNode::Dispatch(variant) => {
                output.extend_from_slice(&[3, 0]);
                put_u16(&mut output, variant.raw());
                put_u16(&mut output, 0);
                put_u16(&mut output, 0);
            }
            ImageNode::Fork(input, branches) => {
                output.extend_from_slice(&[4, *branches]);
                put_u16(&mut output, input.raw());
                put_u16(&mut output, 0);
                put_u16(&mut output, 0);
            }
            ImageNode::Join(types) => {
                output.extend_from_slice(&[
                    5,
                    u8::try_from(types.iter().flatten().count())
                        .expect("verified Join branch count fits u8"),
                ]);
                output.extend_from_slice(&[0; 6]);
            }
            ImageNode::Terminal(kind) => {
                output.extend_from_slice(&[
                    2,
                    match kind {
                        TerminalKind::Success => 1,
                        TerminalKind::KnownFailure => 2,
                        TerminalKind::NotSent => 3,
                        TerminalKind::Unknown => 4,
                    },
                ]);
                output.extend_from_slice(&[0; 6]);
            }
        }
    }
    for edge in &view.edges {
        let (source_kind, source_node, case_tag) = match edge.source {
            EdgeSource::WorkflowInput => (0, 0, 0),
            EdgeSource::ActionSuccess(node) => (1, node.raw(), 0),
            EdgeSource::ActionError(node) => (2, node.raw(), 0),
            EdgeSource::ActionNotSent(node) => (3, node.raw(), 0),
            EdgeSource::ActionUnknown(node) => (4, node.raw(), 0),
            EdgeSource::DispatchCase(node, tag) => (5, node.raw(), tag),
            EdgeSource::ForkBranch(node, branch) => (6, node.raw(), branch),
            EdgeSource::JoinField(node, field) => (7, node.raw(), field),
            EdgeSource::EachItem(region) => (8, u16::from(region), 0),
            EdgeSource::EachOutput(region) => (9, u16::from(region), 0),
        };
        let (target_kind, target_node, target_tag) = match edge.target {
            EdgeTarget::ActionInput(node) => (1, node.raw(), None),
            EdgeTarget::Terminal(node) => (2, node.raw(), None),
            EdgeTarget::DispatchInput(node) => (3, node.raw(), None),
            EdgeTarget::ForkInput(node) => (4, node.raw(), None),
            EdgeTarget::JoinInput(node, branch) => (5, node.raw(), Some(branch)),
            EdgeTarget::EachInput(region) => (6, u16::from(region), None),
            EdgeTarget::EachCollect(region) => (7, u16::from(region), None),
        };
        output.extend_from_slice(&[
            source_kind,
            target_kind,
            u8::from(edge.presentation.is_some()),
            target_tag.unwrap_or(case_tag),
        ]);
        for value in [
            source_node,
            target_node,
            edge.source_type.map_or(0, |item| item.raw()),
            edge.target_type.map_or(0, |item| item.raw()),
            edge.presentation.map_or(0, |item| item.raw()),
            0,
        ] {
            put_u16(&mut output, value);
        }
    }
    for (index, deadline) in view.deadlines.iter().enumerate() {
        let root = index == 0 && deadline.first_node == 0;
        let first_node = if root { 1 } else { deadline.first_node };
        let node_count = if root {
            view.nodes.len() as u16
        } else {
            deadline.node_count
        };
        output.extend_from_slice(&[1, 0]);
        put_u16(&mut output, first_node);
        put_u16(&mut output, node_count);
        put_u16(&mut output, deadline.parent);
        output.extend_from_slice(&deadline.duration_ms.to_le_bytes());
    }
    for region in &view.each_regions {
        output.extend_from_slice(&[2, region.concurrency]);
        put_u16(&mut output, region.template.raw());
        put_u16(&mut output, region.source_list.raw());
        put_u16(&mut output, region.item_input.raw());
        put_u16(&mut output, region.item_output.raw());
        put_u16(&mut output, region.collected.raw());
        put_u16(&mut output, region.bound);
        put_u16(&mut output, 0);
    }
    output.extend_from_slice(&representations);
    output.extend_from_slice(view.name.as_bytes());
    debug_assert_eq!(output.len(), total_size);
    Ok(output)
}

fn read_u16(bytes: &[u8], offset: usize) -> Result<u16, ImageError> {
    let value = bytes.get(offset..offset + 2).ok_or_else(|| {
        ImageError::new(
            ImageErrorCode::Truncated,
            Some(offset),
            "missing 16-bit field",
        )
    })?;
    Ok(u16::from_le_bytes([value[0], value[1]]))
}

fn read_u32(bytes: &[u8], offset: usize) -> Result<u32, ImageError> {
    let value = bytes.get(offset..offset + 4).ok_or_else(|| {
        ImageError::new(
            ImageErrorCode::Truncated,
            Some(offset),
            "missing 32-bit field",
        )
    })?;
    Ok(u32::from_le_bytes([value[0], value[1], value[2], value[3]]))
}

fn read_u64(bytes: &[u8], offset: usize) -> Result<u64, ImageError> {
    let chunk = bytes
        .get(offset..offset + 8)
        .ok_or_else(|| ImageError::new(ImageErrorCode::Truncated, Some(offset), "truncated u64"))?;
    Ok(u64::from_le_bytes(
        chunk.try_into().expect("slice length checked"),
    ))
}

fn validate_representation(
    bytes: &[u8],
    offset: usize,
    end: usize,
    depth: usize,
) -> Result<usize, ImageError> {
    if depth > 64 || offset.checked_add(8).is_none_or(|next| next > end) {
        return Err(ImageError::new(
            ImageErrorCode::InvalidRecord,
            Some(offset),
            "truncated or excessively nested representation descriptor",
        ));
    }
    let kind = bytes[offset];
    let reserved = bytes[offset + 1];
    let children = read_u16(bytes, offset + 2)? as usize;
    let bound = read_u32(bytes, offset + 4)?;
    let valid_header = reserved == 0
        && match kind {
            1..=3 => children == 0 && bound == 0,
            4 | 5 => children == 0 && (1..=1_048_576).contains(&bound),
            6 => bound == 0,
            7 => children == 1 && (1..=256).contains(&bound),
            8 => children != 0 && bound == 0,
            _ => false,
        };
    if !valid_header {
        return Err(ImageError::new(
            ImageErrorCode::InvalidRecord,
            Some(offset),
            "invalid representation descriptor header",
        ));
    }
    let mut cursor = offset + 8;
    for _ in 0..children {
        cursor = validate_representation(bytes, cursor, end, depth + 1)?;
    }
    Ok(cursor)
}

fn representation_child_range(
    bytes: &[u8],
    descriptor: usize,
    descriptor_end: usize,
    child_index: usize,
) -> Result<Option<(usize, usize)>, ImageError> {
    let child_count = usize::from(read_u16(bytes, descriptor + 2)?);
    if child_index >= child_count {
        return Ok(None);
    }
    let mut cursor = descriptor + 8;
    for index in 0..child_count {
        let next = validate_representation(bytes, cursor, descriptor_end, 1)?;
        if index == child_index {
            return Ok(Some((cursor, next)));
        }
        cursor = next;
    }
    Ok(None)
}

pub fn decode_graph_image(bytes: &[u8]) -> Result<DecodedImage, ImageError> {
    if bytes.len() < HEADER_SIZE {
        return Err(ImageError::new(
            ImageErrorCode::Truncated,
            None,
            "image is smaller than its fixed header",
        ));
    }
    if bytes.get(0..4) != Some(MAGIC) {
        return Err(ImageError::new(
            ImageErrorCode::BadMagic,
            Some(0),
            "bad graph-image magic",
        ));
    }
    if read_u16(bytes, 4)? != VERSION {
        return Err(ImageError::new(
            ImageErrorCode::UnsupportedVersion,
            Some(4),
            "unsupported graph-image version",
        ));
    }
    if read_u16(bytes, 6)? as usize != HEADER_SIZE
        || read_u32(bytes, 8)? as usize != bytes.len()
        || read_u32(bytes, 12)? != 0
        || read_u16(bytes, 70)? != 0
        || read_u32(bytes, 76)? != 0
    {
        return Err(ImageError::new(
            ImageErrorCode::InvalidHeader,
            None,
            "invalid size, flags, or reserved header field",
        ));
    }
    let name_offset = read_u32(bytes, 16)? as usize;
    let name_length = read_u16(bytes, 20)? as usize;
    let input_type = read_u16(bytes, 22)?;
    let success_type = read_u16(bytes, 24)?;
    let error_count = read_u16(bytes, 26)? as usize;
    let app_count = read_u16(bytes, 28)? as usize;
    let node_count = read_u16(bytes, 30)? as usize;
    let edge_count = read_u16(bytes, 32)? as usize;
    let type_count = read_u16(bytes, 34)? as usize;
    let region_count = read_u16(bytes, 68)? as usize;
    if input_type == 0
        || success_type == 0
        || error_count == 0
        || error_count > MAX_IMAGE_ERRORS
        || app_count == 0
        || app_count > MAX_GRAPH_NODES
        || type_count == 0
        || type_count > MAX_IMAGE_ERRORS
        || node_count > MAX_GRAPH_NODES
        || edge_count > MAX_GRAPH_EDGES
        || region_count > 16
    {
        return Err(ImageError::new(
            ImageErrorCode::InvalidCount,
            None,
            "invalid type ID or table count",
        ));
    }
    let errors_offset = read_u32(bytes, 36)? as usize;
    let apps_offset = read_u32(bytes, 40)? as usize;
    let types_offset = read_u32(bytes, 44)? as usize;
    let nodes_offset = read_u32(bytes, 48)? as usize;
    let edges_offset = read_u32(bytes, 52)? as usize;
    let representations_offset = read_u32(bytes, 56)? as usize;
    let strings_offset = read_u32(bytes, 60)? as usize;
    let representations_length = read_u32(bytes, 64)? as usize;
    let regions_offset = read_u32(bytes, 72)? as usize;
    let expected_apps = align4(HEADER_SIZE + error_count * 2)?;
    let expected_types = expected_apps + app_count * APP_RECORD_SIZE;
    let expected_nodes = expected_types + type_count * TYPE_RECORD_SIZE;
    let expected_edges = expected_nodes + node_count * NODE_RECORD_SIZE;
    let expected_regions = expected_edges + edge_count * EDGE_RECORD_SIZE;
    let expected_representations = expected_regions + region_count * REGION_RECORD_SIZE;
    let expected_strings = expected_representations
        .checked_add(representations_length)
        .ok_or_else(|| ImageError::new(ImageErrorCode::InvalidOffset, None, "size overflow"))?;
    if errors_offset != HEADER_SIZE
        || apps_offset != expected_apps
        || types_offset != expected_types
        || nodes_offset != expected_nodes
        || edges_offset != expected_edges
        || regions_offset != expected_regions
        || representations_offset != expected_representations
        || strings_offset != expected_strings
        || name_offset != strings_offset
        || name_offset.checked_add(name_length) != Some(bytes.len())
    {
        return Err(ImageError::new(
            ImageErrorCode::InvalidOffset,
            None,
            "table offsets are not canonical or exceed the image",
        ));
    }
    let name_bytes = &bytes[name_offset..];
    let workflow_name = std::str::from_utf8(name_bytes)
        .ok()
        .filter(|name| !name.is_empty())
        .ok_or_else(|| {
            ImageError::new(
                ImageErrorCode::InvalidString,
                Some(name_offset),
                "workflow name is empty or invalid UTF-8",
            )
        })?
        .to_owned();
    let errors = (0..error_count)
        .map(|index| read_u16(bytes, errors_offset + index * 2))
        .collect::<Result<BTreeSet<_>, _>>()?;
    if errors.len() != error_count || errors.contains(&0) {
        return Err(ImageError::new(
            ImageErrorCode::DuplicateRecord,
            Some(errors_offset),
            "workflow error IDs must be unique and nonzero",
        ));
    }
    let mut apps = BTreeSet::new();
    for index in 0..app_count {
        let offset = apps_offset + index * APP_RECORD_SIZE;
        let app = read_u16(bytes, offset)?;
        if app == 0 || read_u16(bytes, offset + 2)? != 0 || !apps.insert(app) {
            return Err(ImageError::new(
                ImageErrorCode::InvalidRecord,
                Some(offset),
                "invalid or duplicate app record",
            ));
        }
    }
    let mut types = BTreeSet::new();
    let mut type_descriptors = BTreeMap::new();
    let mut representation_cursor = representations_offset;
    for index in 0..type_count {
        let offset = types_offset + index * TYPE_RECORD_SIZE;
        let type_id = read_u16(bytes, offset)?;
        let representation_offset = read_u32(bytes, offset + 4)? as usize;
        if type_id == 0
            || read_u16(bytes, offset + 2)? != 0
            || !types.insert(type_id)
            || representation_offset != representation_cursor
        {
            return Err(ImageError::new(
                ImageErrorCode::InvalidRecord,
                Some(offset),
                "invalid, duplicate, or noncanonical type record",
            ));
        }
        let descriptor_end =
            validate_representation(bytes, representation_cursor, strings_offset, 0)?;
        type_descriptors.insert(type_id, (representation_cursor, descriptor_end));
        representation_cursor = descriptor_end;
    }
    if representation_cursor != strings_offset
        || !types.contains(&input_type)
        || !types.contains(&success_type)
        || !errors.iter().all(|type_id| types.contains(type_id))
    {
        return Err(ImageError::new(
            ImageErrorCode::InvalidRecord,
            Some(types_offset),
            "representation descriptors or workflow boundary types are incomplete",
        ));
    }
    let mut action_nodes = BTreeSet::new();
    let mut dispatch_nodes = BTreeMap::new();
    let mut fork_nodes = BTreeMap::new();
    let mut join_nodes = BTreeMap::new();
    let mut terminals = BTreeSet::new();
    let mut terminal_nodes = BTreeMap::new();
    for index in 0..node_count {
        let offset = nodes_offset + index * NODE_RECORD_SIZE;
        let kind = bytes[offset];
        let subtype = bytes[offset + 1];
        let action = read_u16(bytes, offset + 2)?;
        let app = read_u16(bytes, offset + 4)?;
        let reserved = read_u16(bytes, offset + 6)?;
        let node_id = u16::try_from(index + 1).expect("node count is bounded");
        match kind {
            1 if subtype == 0 && action != 0 && apps.contains(&app) && reserved == 0 => {
                action_nodes.insert(node_id);
            }
            3 if subtype == 0
                && action != 0
                && app == 0
                && reserved == 0
                && type_descriptors
                    .get(&action)
                    .is_some_and(|(descriptor, _)| bytes[*descriptor] == 8) =>
            {
                let descriptor = type_descriptors[&action].0;
                let cases = read_u16(bytes, descriptor + 2)?;
                if usize::from(cases) > MAX_GRAPH_EDGES || cases > u16::from(u8::MAX) + 1 {
                    return Err(ImageError::new(
                        ImageErrorCode::InvalidRecord,
                        Some(offset),
                        "Dispatch variant has too many cases",
                    ));
                }
                dispatch_nodes.insert(node_id, (action, cases));
            }
            4 if (2..=crate::graph::MAX_ALL_BRANCHES as u8).contains(&subtype)
                && action != 0
                && types.contains(&action)
                && app == 0
                && reserved == 0 =>
            {
                fork_nodes.insert(node_id, (action, subtype));
            }
            5 if (2..=crate::graph::MAX_ALL_BRANCHES as u8).contains(&subtype)
                && action == 0
                && app == 0
                && reserved == 0 =>
            {
                join_nodes.insert(node_id, subtype);
            }
            2 if (1..=4).contains(&subtype)
                && action == 0
                && app == 0
                && reserved == 0
                && terminals.insert(subtype) =>
            {
                terminal_nodes.insert(node_id, subtype);
            }
            _ => {
                return Err(ImageError::new(
                    ImageErrorCode::InvalidRecord,
                    Some(offset),
                    "invalid node record",
                ));
            }
        }
    }
    if terminals != BTreeSet::from([1, 2, 3, 4]) {
        return Err(ImageError::new(
            ImageErrorCode::InvalidTopology,
            Some(nodes_offset),
            "terminal kinds must occur exactly once",
        ));
    }
    let mut deadline_ranges = Vec::new();
    let mut each_regions = BTreeMap::new();
    for index in 0..region_count {
        let offset = regions_offset + index * REGION_RECORD_SIZE;
        match bytes[offset] {
            1 => {
                let first = usize::from(read_u16(bytes, offset + 2)?);
                let count = usize::from(read_u16(bytes, offset + 4)?);
                let parent = usize::from(read_u16(bytes, offset + 6)?);
                let end = first
                    .checked_add(count)
                    .and_then(|value| value.checked_sub(1));
                let within_parent = if parent == 0 {
                    true
                } else {
                    deadline_ranges.get(parent - 1).is_some_and(
                        |(parent_first, parent_end): &(usize, usize)| {
                            first >= *parent_first && end.is_some_and(|end| end <= *parent_end)
                        },
                    )
                };
                if bytes[offset + 1] != 0
                    || first == 0
                    || count == 0
                    || end.is_none_or(|end| end > node_count)
                    || parent > deadline_ranges.len()
                    || !within_parent
                    || (deadline_ranges.is_empty()
                        && (first != 1 || count != node_count || parent != 0))
                    || read_u64(bytes, offset + 8)? == 0
                    || !each_regions.is_empty()
                {
                    return Err(ImageError::new(
                        ImageErrorCode::InvalidRecord,
                        Some(offset),
                        "invalid deadline region",
                    ));
                }
                deadline_ranges.push((first, end.expect("validated nonempty region")));
            }
            2 => {
                let id = u16::try_from(each_regions.len() + 1).expect("region count is bounded");
                let concurrency = bytes[offset + 1];
                let template = read_u16(bytes, offset + 2)?;
                let source_list = read_u16(bytes, offset + 4)?;
                let item_input = read_u16(bytes, offset + 6)?;
                let item_output = read_u16(bytes, offset + 8)?;
                let collected = read_u16(bytes, offset + 10)?;
                let bound = read_u16(bytes, offset + 12)?;
                let source_descriptor = type_descriptors.get(&source_list);
                let collected_descriptor = type_descriptors.get(&collected);
                let input_descriptor = type_descriptors.get(&item_input);
                let output_descriptor = type_descriptors.get(&item_output);
                let source_matches = source_descriptor.zip(input_descriptor).is_some_and(
                    |((list_start, list_end), (item_start, item_end))| {
                        bytes[*list_start] == 7
                            && read_u32(bytes, *list_start + 4).ok() == Some(u32::from(bound))
                            && representation_child_range(bytes, *list_start, *list_end, 0)
                                .ok()
                                .flatten()
                                .is_some_and(|(child_start, child_end)| {
                                    bytes[child_start..child_end] == bytes[*item_start..*item_end]
                                })
                    },
                );
                let output_matches = collected_descriptor.zip(output_descriptor).is_some_and(
                    |((list_start, list_end), (item_start, item_end))| {
                        bytes[*list_start] == 7
                            && read_u32(bytes, *list_start + 4)
                                .is_ok_and(|maximum| maximum >= u32::from(bound))
                            && representation_child_range(bytes, *list_start, *list_end, 0)
                                .ok()
                                .flatten()
                                .is_some_and(|(child_start, child_end)| {
                                    bytes[child_start..child_end] == bytes[*item_start..*item_end]
                                })
                    },
                );
                if !(1..=8).contains(&concurrency)
                    || u16::from(concurrency) > bound
                    || bound == 0
                    || !action_nodes.contains(&template)
                    || !source_matches
                    || !output_matches
                    || read_u16(bytes, offset + 14)? != 0
                {
                    return Err(ImageError::new(
                        ImageErrorCode::InvalidRecord,
                        Some(offset),
                        "invalid Each region",
                    ));
                }
                each_regions.insert(
                    id,
                    (
                        template,
                        source_list,
                        item_input,
                        item_output,
                        collected,
                        bound,
                        concurrency,
                    ),
                );
            }
            _ => {
                return Err(ImageError::new(
                    ImageErrorCode::InvalidRecord,
                    Some(offset),
                    "unknown region kind",
                ));
            }
        }
    }
    let mut edge_keys = BTreeSet::new();
    let mut input_targets = BTreeMap::<u16, usize>::new();
    let mut source_ports = BTreeMap::<(u8, u16, u8), usize>::new();
    let mut roots = Vec::new();
    let mut adjacency = BTreeMap::<u16, Vec<u16>>::new();
    let mut join_input_types = BTreeMap::<(u16, u8), u16>::new();
    let mut join_output_types = BTreeMap::<(u16, u8), u16>::new();
    let mut each_input_ports = BTreeMap::<u16, usize>::new();
    let mut each_collect_ports = BTreeMap::<u16, usize>::new();
    for index in 0..edge_count {
        let offset = edges_offset + index * EDGE_RECORD_SIZE;
        let source_kind = bytes[offset];
        let target_kind = bytes[offset + 1];
        let flags = bytes[offset + 2];
        let case_tag = bytes[offset + 3];
        let source_node = read_u16(bytes, offset + 4)?;
        let target_node = read_u16(bytes, offset + 6)?;
        let source_type = read_u16(bytes, offset + 8)?;
        let target_type = read_u16(bytes, offset + 10)?;
        let presentation = read_u16(bytes, offset + 12)?;
        let reserved = read_u16(bytes, offset + 14)?;
        let valid_dispatch_source = if source_kind == 5 {
            dispatch_nodes
                .get(&source_node)
                .and_then(|(variant, count)| {
                    (usize::from(case_tag) < usize::from(*count))
                        .then_some(type_descriptors.get(variant)?)
                })
                .and_then(|(variant_descriptor, variant_end)| {
                    representation_child_range(
                        bytes,
                        *variant_descriptor,
                        *variant_end,
                        usize::from(case_tag),
                    )
                    .ok()
                    .flatten()
                })
                .zip(type_descriptors.get(&source_type))
                .is_some_and(|((child_start, child_end), (source_start, source_end))| {
                    bytes[child_start..child_end] == bytes[*source_start..*source_end]
                })
        } else {
            false
        };
        let valid_source = match source_kind {
            0 => source_node == 0 && source_type == input_type && types.contains(&source_type),
            1 | 2 => action_nodes.contains(&source_node) && types.contains(&source_type),
            3 | 4 => action_nodes.contains(&source_node) && source_type == 0 && target_type == 0,
            5 => valid_dispatch_source,
            6 => fork_nodes
                .get(&source_node)
                .is_some_and(|(input, branches)| case_tag < *branches && source_type == *input),
            7 => join_nodes
                .get(&source_node)
                .is_some_and(|branches| case_tag < *branches && types.contains(&source_type)),
            8 => each_regions
                .get(&source_node)
                .is_some_and(|(_, _, item_input, _, _, _, _)| {
                    case_tag == 0 && source_type == *item_input
                }),
            9 => each_regions
                .get(&source_node)
                .is_some_and(|(_, _, _, _, collected, _, _)| {
                    case_tag == 0 && source_type == *collected
                }),
            _ => false,
        };
        let valid_target = match target_kind {
            1 => action_nodes.contains(&target_node) && types.contains(&target_type),
            2 => terminal_nodes
                .get(&target_node)
                .is_some_and(|subtype| match subtype {
                    1 => target_type == success_type,
                    2 => errors.contains(&target_type),
                    3 | 4 => target_type == 0,
                    _ => false,
                }),
            3 => dispatch_nodes
                .get(&target_node)
                .is_some_and(|(variant, _)| target_type == *variant),
            4 => fork_nodes
                .get(&target_node)
                .is_some_and(|(input, _)| target_type == *input),
            5 => join_nodes
                .get(&target_node)
                .is_some_and(|branches| case_tag < *branches && types.contains(&target_type)),
            6 => each_regions
                .get(&target_node)
                .is_some_and(|(_, source_list, _, _, _, _, _)| {
                    case_tag == 0 && target_type == *source_list
                }),
            7 => each_regions
                .get(&target_node)
                .is_some_and(|(_, _, _, item_output, _, _, _)| {
                    case_tag == 0 && target_type == *item_output
                }),
            _ => false,
        };
        let valid_presentation =
            (flags == 0 && presentation == 0) || (flags == 1 && presentation == target_type);
        if !valid_source
            || !valid_target
            || !valid_presentation
            || (source_kind != 5
                && source_kind != 6
                && source_kind != 7
                && source_kind != 8
                && source_kind != 9
                && target_kind != 5
                && case_tag != 0)
            || reserved != 0
        {
            return Err(ImageError::new(
                ImageErrorCode::InvalidRecord,
                Some(offset),
                "invalid edge record",
            ));
        }
        if !edge_keys.insert((source_kind, source_node, case_tag, target_kind, target_node)) {
            return Err(ImageError::new(
                ImageErrorCode::DuplicateRecord,
                Some(offset),
                "duplicate edge",
            ));
        }
        if target_kind == 1 || target_kind == 3 || target_kind == 4 {
            *input_targets.entry(target_node).or_default() += 1;
        }
        if target_kind == 5
            && join_input_types
                .insert((target_node, case_tag), target_type)
                .is_some()
        {
            return Err(ImageError::new(
                ImageErrorCode::DuplicateRecord,
                Some(offset),
                "duplicate Join input",
            ));
        }
        if target_kind == 6 {
            *each_input_ports.entry(target_node).or_default() += 1;
        }
        if target_kind == 7 {
            *each_collect_ports.entry(target_node).or_default() += 1;
        }
        if source_kind == 7
            && join_output_types
                .insert((source_node, case_tag), source_type)
                .is_some()
        {
            return Err(ImageError::new(
                ImageErrorCode::DuplicateRecord,
                Some(offset),
                "duplicate Join output",
            ));
        }
        let source_port_tag = if matches!(source_kind, 5..=7) {
            case_tag
        } else {
            0
        };
        *source_ports
            .entry((source_kind, source_node, source_port_tag))
            .or_default() += 1;
        let graph_target = match target_kind {
            6 => each_regions.get(&target_node).map(|region| region.0),
            7 => None,
            _ => Some(target_node),
        };
        let graph_source = match source_kind {
            8 => None,
            9 => each_regions.get(&source_node).map(|region| region.0),
            _ => (source_kind != 0).then_some(source_node),
        };
        if source_kind == 0 {
            if let Some(target) = graph_target {
                roots.push(target);
            }
        } else if let (Some(source), Some(target)) = (graph_source, graph_target) {
            adjacency.entry(source).or_default().push(target);
        }
    }
    if action_nodes
        .iter()
        .any(|node| input_targets.get(node) != Some(&1))
        || dispatch_nodes
            .keys()
            .any(|node| input_targets.get(node) != Some(&1))
        || fork_nodes
            .keys()
            .any(|node| input_targets.get(node) != Some(&1))
        || source_ports.get(&(0, 0, 0)) != Some(&1)
        || action_nodes.iter().any(|node| {
            source_ports.get(&(1, *node, 0)) != Some(&1)
                || source_ports.get(&(2, *node, 0)) != Some(&1)
                || source_ports.get(&(3, *node, 0)) != Some(&1)
                || source_ports.get(&(4, *node, 0)) != Some(&1)
        })
        || dispatch_nodes.iter().any(|(node, (_, count))| {
            (0..*count).any(|tag| source_ports.get(&(5, *node, tag as u8)) != Some(&1))
        })
        || fork_nodes.iter().any(|(node, (_, count))| {
            (0..*count).any(|tag| source_ports.get(&(6, *node, tag)) != Some(&1))
        })
        || join_nodes.iter().any(|(node, count)| {
            let used_outputs = (0..*count)
                .filter(|tag| join_output_types.contains_key(&(*node, *tag)))
                .count();
            used_outputs == 0
                || (0..*count).any(|tag| {
                    let input = join_input_types.get(&(*node, tag));
                    let output = join_output_types.get(&(*node, tag));
                    input.is_none() || output.is_some_and(|type_id| Some(type_id) != input)
                })
        })
        || each_regions.iter().any(|(region, _)| {
            each_input_ports.get(region) != Some(&1)
                || each_collect_ports.get(region) != Some(&1)
                || source_ports.get(&(8, *region, 0)) != Some(&1)
                || source_ports.get(&(9, *region, 0)) != Some(&1)
        })
    {
        return Err(ImageError::new(
            ImageErrorCode::InvalidTopology,
            Some(edges_offset),
            "required graph ports are missing",
        ));
    }
    let mut reachable = BTreeSet::new();
    let mut queue = VecDeque::from(roots);
    while let Some(node) = queue.pop_front() {
        if !reachable.insert(node) {
            continue;
        }
        if let Some(targets) = adjacency.get(&node) {
            queue.extend(targets.iter().copied());
        }
    }
    let all_nodes = (1..=node_count)
        .map(|node| u16::try_from(node).expect("node count is bounded"))
        .collect::<BTreeSet<_>>();
    if reachable != all_nodes {
        return Err(ImageError::new(
            ImageErrorCode::InvalidTopology,
            Some(edges_offset),
            "graph contains an unreachable node or cycle",
        ));
    }
    Ok(DecodedImage {
        workflow_name,
        input_type,
        success_type,
        error_count,
        app_count,
        type_count,
        node_count,
        edge_count,
        region_count,
    })
}

fn payload_u32(payload: &[u8], offset: usize) -> Result<u32, ImageError> {
    let value = payload.get(offset..offset + 4).ok_or_else(|| {
        ImageError::new(
            ImageErrorCode::InvalidValue,
            Some(offset),
            "truncated value field",
        )
    })?;
    Ok(u32::from_le_bytes(
        value.try_into().expect("four-byte slice"),
    ))
}

fn validate_value_inner(
    image: &[u8],
    representation_offset: usize,
    representation_end: usize,
    payload: &[u8],
    payload_offset: usize,
    depth: usize,
) -> Result<(usize, usize), ImageError> {
    if depth > 64 || representation_offset + 8 > representation_end {
        return Err(ImageError::new(
            ImageErrorCode::InvalidValue,
            Some(payload_offset),
            "invalid representation while checking value",
        ));
    }
    let kind = image[representation_offset];
    let children = read_u16(image, representation_offset + 2)? as usize;
    let bound = read_u32(image, representation_offset + 4)? as usize;
    let child_start = representation_offset + 8;
    match kind {
        1 => Ok((child_start, payload_offset)),
        2 => {
            let end = payload_offset.checked_add(8).ok_or_else(|| {
                ImageError::new(
                    ImageErrorCode::InvalidValue,
                    Some(payload_offset),
                    "size overflow",
                )
            })?;
            if end > payload.len() {
                return Err(ImageError::new(
                    ImageErrorCode::InvalidValue,
                    Some(payload_offset),
                    "truncated Integer",
                ));
            }
            Ok((child_start, end))
        }
        3 => {
            let value = payload.get(payload_offset).copied().ok_or_else(|| {
                ImageError::new(
                    ImageErrorCode::InvalidValue,
                    Some(payload_offset),
                    "truncated Boolean",
                )
            })?;
            if value > 1 {
                return Err(ImageError::new(
                    ImageErrorCode::InvalidValue,
                    Some(payload_offset),
                    "Boolean must be zero or one",
                ));
            }
            Ok((child_start, payload_offset + 1))
        }
        4 | 5 => {
            let length = payload_u32(payload, payload_offset)? as usize;
            if payload_u32(payload, payload_offset + 4)? != 0 || length > bound {
                return Err(ImageError::new(
                    ImageErrorCode::InvalidValue,
                    Some(payload_offset),
                    "invalid bounded byte length or reserved word",
                ));
            }
            let data_start = payload_offset + 8;
            let data_end = data_start.checked_add(length).ok_or_else(|| {
                ImageError::new(
                    ImageErrorCode::InvalidValue,
                    Some(payload_offset),
                    "size overflow",
                )
            })?;
            let data = payload.get(data_start..data_end).ok_or_else(|| {
                ImageError::new(
                    ImageErrorCode::InvalidValue,
                    Some(payload_offset),
                    "truncated bounded bytes",
                )
            })?;
            if kind == 4 && (std::str::from_utf8(data).is_err() || data.contains(&0)) {
                return Err(ImageError::new(
                    ImageErrorCode::InvalidValue,
                    Some(data_start),
                    "String must be valid UTF-8 without NUL",
                ));
            }
            Ok((child_start, data_end))
        }
        6 => {
            let mut representation_cursor = child_start;
            let mut payload_cursor = payload_offset;
            for _ in 0..children {
                (representation_cursor, payload_cursor) = validate_value_inner(
                    image,
                    representation_cursor,
                    representation_end,
                    payload,
                    payload_cursor,
                    depth + 1,
                )?;
            }
            Ok((representation_cursor, payload_cursor))
        }
        7 => {
            let count = payload_u32(payload, payload_offset)? as usize;
            if payload_u32(payload, payload_offset + 4)? != 0 || count > bound {
                return Err(ImageError::new(
                    ImageErrorCode::InvalidValue,
                    Some(payload_offset),
                    "invalid list count or reserved word",
                ));
            }
            let representation_after_child =
                validate_representation(image, child_start, representation_end, depth + 1)?;
            let mut payload_cursor = payload_offset + 8;
            for _ in 0..count {
                let (after, payload_after) = validate_value_inner(
                    image,
                    child_start,
                    representation_end,
                    payload,
                    payload_cursor,
                    depth + 1,
                )?;
                debug_assert_eq!(after, representation_after_child);
                payload_cursor = payload_after;
            }
            Ok((representation_after_child, payload_cursor))
        }
        8 => {
            let tag = payload_u32(payload, payload_offset)? as usize;
            if payload_u32(payload, payload_offset + 4)? != 0 || tag >= children {
                return Err(ImageError::new(
                    ImageErrorCode::InvalidValue,
                    Some(payload_offset),
                    "invalid variant tag or reserved word",
                ));
            }
            let mut representation_cursor = child_start;
            let mut payload_cursor = payload_offset + 8;
            for child in 0..children {
                if child == tag {
                    (representation_cursor, payload_cursor) = validate_value_inner(
                        image,
                        representation_cursor,
                        representation_end,
                        payload,
                        payload_cursor,
                        depth + 1,
                    )?;
                } else {
                    representation_cursor = validate_representation(
                        image,
                        representation_cursor,
                        representation_end,
                        depth + 1,
                    )?;
                }
            }
            Ok((representation_cursor, payload_cursor))
        }
        _ => Err(ImageError::new(
            ImageErrorCode::InvalidValue,
            Some(payload_offset),
            "unknown representation kind",
        )),
    }
}

pub fn validate_graph_value(image: &[u8], type_id: u16, payload: &[u8]) -> Result<(), ImageError> {
    decode_graph_image(image)?;
    let type_count = read_u16(image, 34)? as usize;
    let types_offset = read_u32(image, 44)? as usize;
    let representations_end = read_u32(image, 60)? as usize;
    let representation_offset = (0..type_count)
        .find_map(|index| {
            let offset = types_offset + index * TYPE_RECORD_SIZE;
            if read_u16(image, offset).ok() == Some(type_id) {
                read_u32(image, offset + 4).ok().map(|value| value as usize)
            } else {
                None
            }
        })
        .ok_or_else(|| {
            ImageError::new(
                ImageErrorCode::InvalidValue,
                None,
                format!("type ID {type_id} is not present in the graph image"),
            )
        })?;
    let (_, consumed) = validate_value_inner(
        image,
        representation_offset,
        representations_end,
        payload,
        0,
        0,
    )?;
    if consumed != payload.len() {
        return Err(ImageError::new(
            ImageErrorCode::InvalidValue,
            Some(consumed),
            "value contains trailing bytes",
        ));
    }
    Ok(())
}
