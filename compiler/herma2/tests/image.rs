use herma2::{
    ImageErrorCode, build_grade_pipeline, decode_graph_image, encode_graph_image,
    validate_graph_value,
};
use sha2::{Digest, Sha256};

fn encoded_pipeline() -> Vec<u8> {
    let (catalog, graph) = build_grade_pipeline().expect("pipeline");
    encode_graph_image(&graph, &catalog).expect("image")
}

fn read_u32(bytes: &[u8], offset: usize) -> usize {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap()) as usize
}

#[test]
fn grade_pipeline_round_trips_through_the_versioned_image() {
    let bytes = encoded_pipeline();
    let decoded = decode_graph_image(&bytes).expect("valid image");
    assert_eq!(decoded.workflow_name, "grade_pipeline");
    assert_eq!(decoded.error_count, 3);
    assert_eq!(decoded.app_count, 3);
    assert_eq!(decoded.node_count, 7);
    assert_eq!(decoded.edge_count, 13);
    assert_eq!(decoded.region_count, 0);
    assert_eq!(&bytes[0..4], b"H2GI");
    assert_eq!(decoded.type_count, 9);
    assert_eq!(bytes.len(), 634);
}

#[test]
fn graph_image_is_deterministic_with_a_golden_digest() {
    let first = encoded_pipeline();
    let second = encoded_pipeline();
    assert_eq!(first, second);
    let digest = Sha256::digest(&first);
    assert_eq!(
        format!("{digest:x}"),
        "e6723be5fb051ceaa2eb4a7279fb60f0bc4c860e39f85b62995adaa2fc09b3eb"
    );
}

#[test]
fn every_truncated_prefix_is_rejected() {
    let bytes = encoded_pipeline();
    for length in 0..bytes.len() {
        assert!(
            decode_graph_image(&bytes[..length]).is_err(),
            "accepted prefix of length {length}"
        );
    }
}

#[test]
fn rejects_header_offsets_counts_and_reserved_fields() {
    let bytes = encoded_pipeline();
    for (offset, value, code) in [
        (0, 0, ImageErrorCode::BadMagic),
        (4, 2, ImageErrorCode::UnsupportedVersion),
        (12, 1, ImageErrorCode::InvalidHeader),
        (26, 0, ImageErrorCode::InvalidCount),
        (36, 0, ImageErrorCode::InvalidOffset),
        (70, 1, ImageErrorCode::InvalidHeader),
    ] {
        let mut malformed = bytes.clone();
        malformed[offset] = value;
        assert_eq!(
            decode_graph_image(&malformed).unwrap_err().code,
            code,
            "mutation at {offset}"
        );
    }
}

#[test]
fn rejects_malformed_app_node_and_edge_records() {
    let bytes = encoded_pipeline();
    let apps = read_u32(&bytes, 40);
    let nodes = read_u32(&bytes, 48);
    let edges = read_u32(&bytes, 52);
    for (offset, code) in [
        (apps + 2, ImageErrorCode::InvalidRecord),
        (nodes, ImageErrorCode::InvalidRecord),
        (edges, ImageErrorCode::InvalidRecord),
        (edges + 3, ImageErrorCode::InvalidRecord),
        (edges + 14, ImageErrorCode::InvalidRecord),
    ] {
        let mut malformed = bytes.clone();
        malformed[offset] = 0xff;
        assert_eq!(
            decode_graph_image(&malformed).unwrap_err().code,
            code,
            "mutation at {offset}"
        );
    }
}

#[test]
fn rejects_duplicate_errors_and_broken_port_topology() {
    let bytes = encoded_pipeline();
    let mut duplicate_error = bytes.clone();
    let first_error: [u8; 2] = duplicate_error[80..82].try_into().unwrap();
    duplicate_error[82..84].copy_from_slice(&first_error);
    assert_eq!(
        decode_graph_image(&duplicate_error).unwrap_err().code,
        ImageErrorCode::DuplicateRecord
    );

    let edges = read_u32(&bytes, 52);
    let mut duplicate_input = bytes.clone();
    let first_target: [u8; 2] = duplicate_input[edges + 6..edges + 8].try_into().unwrap();
    duplicate_input[edges + 16 + 6..edges + 16 + 8].copy_from_slice(&first_target);
    assert!(matches!(
        decode_graph_image(&duplicate_input).unwrap_err().code,
        ImageErrorCode::DuplicateRecord
            | ImageErrorCode::InvalidRecord
            | ImageErrorCode::InvalidTopology
    ));
}

#[test]
fn validates_canonical_payloads_from_embedded_type_descriptors() {
    let (catalog, graph) = build_grade_pipeline().unwrap();
    let image = encode_graph_image(&graph, &catalog).unwrap();
    let empty = catalog.resolve_type("grade-list", "Empty").unwrap().raw();
    let grades = catalog
        .resolve_type("grade-list", "GradeList")
        .unwrap()
        .raw();
    let mean = catalog
        .resolve_type("mean-calculator", "Mean")
        .unwrap()
        .raw();
    let printed = catalog.resolve_type("printer", "Printed").unwrap().raw();

    validate_graph_value(&image, empty, &[]).unwrap();
    let mut grade_payload = Vec::new();
    grade_payload.extend_from_slice(&3u32.to_le_bytes());
    grade_payload.extend_from_slice(&0u32.to_le_bytes());
    for grade in [70i64, 80, 90] {
        grade_payload.extend_from_slice(&grade.to_le_bytes());
    }
    validate_graph_value(&image, grades, &grade_payload).unwrap();
    validate_graph_value(&image, mean, &80i64.to_le_bytes()).unwrap();
    validate_graph_value(&image, printed, &[1]).unwrap();

    let mut bad_count = grade_payload.clone();
    bad_count[0..4].copy_from_slice(&33u32.to_le_bytes());
    assert_eq!(
        validate_graph_value(&image, grades, &bad_count)
            .unwrap_err()
            .code,
        ImageErrorCode::InvalidValue
    );
    let mut bad_reserved = grade_payload.clone();
    bad_reserved[4] = 1;
    assert!(validate_graph_value(&image, grades, &bad_reserved).is_err());
    assert!(validate_graph_value(&image, mean, &[0; 7]).is_err());
    assert!(validate_graph_value(&image, printed, &[2]).is_err());
    assert!(validate_graph_value(&image, printed, &[1, 0]).is_err());
    assert!(validate_graph_value(&image, 999, &[]).is_err());
}

#[test]
fn rejects_malformed_representation_descriptors() {
    let bytes = encoded_pipeline();
    let representations = read_u32(&bytes, 56);
    for offset in [representations, representations + 1, representations + 2] {
        let mut malformed = bytes.clone();
        malformed[offset] = 0xff;
        assert_eq!(
            decode_graph_image(&malformed).unwrap_err().code,
            ImageErrorCode::InvalidRecord
        );
    }
}
