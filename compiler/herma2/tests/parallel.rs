use herma2::{Catalog, compile_hscript, compile_schema, decode_graph_image, encode_graph_image};

fn parallel_catalog() -> Catalog {
    let mut catalog = Catalog::new();
    for (file, source) in [
        (
            "source.hschema2",
            r#"
app source
type Trigger = Unit
type Item = Integer
type SourceError = Unit
action get {
    input Trigger
    success Item
    error SourceError
    kind irreversible
}
"#,
        ),
        (
            "alpha.hschema2",
            r#"
app alpha
type Input = Integer
type Output = Integer
type AlphaError = Unit
action run {
    input Input
    success Output
    error AlphaError
    kind irreversible
}
"#,
        ),
        (
            "beta.hschema2",
            r#"
app beta
type Input = Integer
type Output = Integer
type BetaError = Unit
action run {
    input Input
    success Output
    error BetaError
    kind irreversible
}
"#,
        ),
        (
            "sink.hschema2",
            r#"
app sink
type Input = Integer
type Done = Boolean
type SinkError = Unit
action use {
    input Input
    success Done
    error SinkError
    kind irreversible
}
"#,
        ),
    ] {
        compile_schema(&mut catalog, file, source).unwrap();
    }
    catalog
}

const WORKFLOW: &str = r#"
workflow cooperate(input: source::Trigger) -> sink::Done
errors {
    source::SourceError
    alpha::AlphaError
    beta::BetaError
    sink::SinkError
}
{
    let item = input |> source/get()
    let results = all item {
        left = item |> as alpha::Input |> alpha/run()
        right = item |> as beta::Input |> beta/run()
    }
    return results.left |> as sink::Input |> sink/use()
}
"#;

#[test]
fn typed_all_lowers_to_bounded_fork_and_join() {
    let catalog = parallel_catalog();
    let graph = compile_hscript(&catalog, "cooperate.hscript2", WORKFLOW).unwrap();
    let explanation = graph.explain(&catalog);
    assert!(explanation.contains("fork source::Item branches=2"));
    assert!(explanation.contains("join [alpha::Output, beta::Output]"));
    assert_eq!(
        graph.resources(&catalog).to_string(),
        "actions=4 dispatches=0 forks=1 joins=1 deadline_regions=0 each_regions=0 terminals=4 edges=20 apps=4 max_concurrent_actions=2 max_payload_bytes=8"
    );
    let image = encode_graph_image(&graph, &catalog).unwrap();
    let decoded = decode_graph_image(&image).unwrap();
    assert_eq!(decoded.node_count, 10);
    assert_eq!(decoded.edge_count, 20);

    let nodes_offset = u32::from_le_bytes(image[48..52].try_into().unwrap()) as usize;
    let fork_offset = (0..decoded.node_count)
        .map(|index| nodes_offset + index * 8)
        .find(|offset| image[*offset] == 4)
        .unwrap();
    let mut invalid_fork = image.clone();
    invalid_fork[fork_offset + 1] = 1;
    assert!(decode_graph_image(&invalid_fork).is_err());

    let edges_offset = u32::from_le_bytes(image[52..56].try_into().unwrap()) as usize;
    let join_output = (0..decoded.edge_count)
        .map(|index| edges_offset + index * 16)
        .find(|offset| image[*offset] == 7)
        .unwrap();
    let mut wrong_join_type = image.clone();
    let workflow_input_type = [wrong_join_type[22], wrong_join_type[23]];
    wrong_join_type[join_output + 8..join_output + 10].copy_from_slice(&workflow_input_type);
    assert!(decode_graph_image(&wrong_join_type).is_err());
}

#[test]
fn typed_all_rejects_non_static_or_duplicate_branches() {
    let catalog = parallel_catalog();
    let too_small = WORKFLOW.replace("        right = item |> as beta::Input |> beta/run()\n", "");
    let error = compile_hscript(&catalog, "small.hscript2", &too_small).unwrap_err();
    assert_eq!(error.code, "all-branch-count");

    let duplicate = WORKFLOW.replace("right = item", "left = item");
    let error = compile_hscript(&catalog, "duplicate.hscript2", &duplicate).unwrap_err();
    assert_eq!(error.code, "duplicate-all-field");
}

#[test]
fn typed_all_rejects_a_branch_that_escapes_the_shared_input() {
    let catalog = parallel_catalog();
    let wrong = WORKFLOW.replace("right = item", "right = results.left");
    let error = compile_hscript(&catalog, "wrong-source.hscript2", &wrong).unwrap_err();
    assert_eq!(error.code, "invalid-all-branch");
}
