use herma2::{
    Catalog, EdgeSource, EdgeTarget, GraphBuilder, GraphErrorCode, TerminalKind, compile_hscript,
    compile_schema, decode_graph_image, encode_graph_image,
};

fn choice_catalog() -> (Catalog, herma2::SchemaContract) {
    let source = r#"
app decision

type Trigger = Unit
type Approved = Integer
type Rejected = String<16>
type Decision = variant {
    approved: Approved
    rejected: Rejected
}
type Done = Boolean
type DecisionError = Unit

action decide {
    input Trigger
    success Decision
    error DecisionError
    compensation none
}
action accept {
    input Approved
    success Done
    error DecisionError
    compensation none
}
action reject {
    input Rejected
    success Done
    error DecisionError
    compensation none
}
"#;
    let mut catalog = Catalog::new();
    let contract = compile_schema(&mut catalog, "decision.hschema2", source).unwrap();
    (catalog, contract)
}

#[test]
fn hscript_match_lowers_to_terminal_dispatch_branches() {
    let (catalog, _) = choice_catalog();
    let source = r#"
workflow choose(input: decision::Trigger) -> decision::Done
errors { decision::DecisionError }
{
    let decision = input |> decision/decide()
    match decision {
        case approved value {
            return value |> decision/accept()
        }
        case rejected reason {
            return reason |> decision/reject()
        }
    }
}
"#;
    let graph = compile_hscript(&catalog, "choose.hscript2", source).unwrap();
    assert!(graph.has_complete_source_map());
    assert_eq!(
        graph.resources(&catalog).to_string(),
        "actions=3 dispatches=1 forks=0 joins=0 deadline_regions=0 each_regions=0 terminals=4 edges=15 apps=1 max_concurrent_actions=1 max_payload_bytes=32"
    );
    assert!(
        graph
            .explain(&catalog)
            .contains("dispatch decision::Decision")
    );
    let image = encode_graph_image(&graph, &catalog).unwrap();
    let decoded = decode_graph_image(&image).unwrap();
    assert_eq!(decoded.node_count, 8);
    assert_eq!(decoded.edge_count, 15);

    let edges_offset = u32::from_le_bytes(image[52..56].try_into().unwrap()) as usize;
    let case_edge = (0..decoded.edge_count)
        .map(|index| edges_offset + index * 16)
        .find(|offset| image[*offset] == 5)
        .unwrap();
    let mut bad_tag = image.clone();
    bad_tag[case_edge + 3] = 9;
    assert!(decode_graph_image(&bad_tag).is_err());

    let mut bad_payload_type = image.clone();
    bad_payload_type[case_edge + 8..case_edge + 10].copy_from_slice(&5u16.to_le_bytes());
    assert!(decode_graph_image(&bad_payload_type).is_err());
}

#[test]
fn hscript_match_rejects_missing_and_duplicate_cases() {
    let (catalog, _) = choice_catalog();
    let missing = r#"
workflow choose(input: decision::Trigger) -> decision::Done
errors { decision::DecisionError }
{
    let decision = input |> decision/decide()
    match decision {
        case approved value { value |> decision/accept() }
    }
}
"#;
    let diagnostic = compile_hscript(&catalog, "missing.hscript2", missing).unwrap_err();
    assert_eq!(diagnostic.code, "non-exhaustive-match");

    let duplicate = r#"
workflow choose(input: decision::Trigger) -> decision::Done
errors { decision::DecisionError }
{
    let decision = input |> decision/decide()
    match decision {
        case approved first { first |> decision/accept() }
        case approved second { second |> decision/accept() }
        case rejected reason { reason |> decision/reject() }
    }
}
"#;
    let diagnostic = compile_hscript(&catalog, "duplicate.hscript2", duplicate).unwrap_err();
    assert_eq!(diagnostic.code, "duplicate-match-case");
}

#[test]
fn named_variant_dispatch_is_exhaustive_and_typed() {
    let (catalog, contract) = choice_catalog();
    let trigger = contract.types["Trigger"];
    let decision = contract.types["Decision"];
    let done = contract.types["Done"];
    let error = contract.types["DecisionError"];
    let mut builder = GraphBuilder::new("choose", trigger, done, vec![error]);
    let decide = builder.add_action(contract.actions["decide"]);
    let dispatch = builder.add_dispatch(decision);
    let accept = builder.add_action(contract.actions["accept"]);
    let reject = builder.add_action(contract.actions["reject"]);
    let success = builder.add_terminal(TerminalKind::Success);
    let failure = builder.add_terminal(TerminalKind::KnownFailure);
    let not_sent = builder.add_terminal(TerminalKind::NotSent);
    let unknown = builder.add_terminal(TerminalKind::Unknown);

    builder.connect(
        EdgeSource::WorkflowInput,
        EdgeTarget::ActionInput(decide),
        None,
    );
    builder.connect(
        EdgeSource::ActionSuccess(decide),
        EdgeTarget::DispatchInput(dispatch),
        None,
    );
    builder.connect(
        EdgeSource::DispatchCase(dispatch, 0),
        EdgeTarget::ActionInput(accept),
        None,
    );
    builder.connect(
        EdgeSource::DispatchCase(dispatch, 1),
        EdgeTarget::ActionInput(reject),
        None,
    );
    for action in [decide, accept, reject] {
        builder.connect(
            EdgeSource::ActionError(action),
            EdgeTarget::Terminal(failure),
            None,
        );
        builder.connect(
            EdgeSource::ActionNotSent(action),
            EdgeTarget::Terminal(not_sent),
            None,
        );
        builder.connect(
            EdgeSource::ActionUnknown(action),
            EdgeTarget::Terminal(unknown),
            None,
        );
    }
    for action in [accept, reject] {
        builder.connect(
            EdgeSource::ActionSuccess(action),
            EdgeTarget::Terminal(success),
            None,
        );
    }
    let graph = builder.finish(&catalog).expect("dispatch graph");
    assert_eq!(
        graph.resources(&catalog).to_string(),
        "actions=3 dispatches=1 forks=0 joins=0 deadline_regions=0 each_regions=0 terminals=4 edges=15 apps=1 max_concurrent_actions=1 max_payload_bytes=32"
    );
    let explanation = graph.explain(&catalog);
    assert!(explanation.contains("dispatch decision::Decision"));
    assert!(explanation.contains("case[0] -> n3.input : decision::Approved"));
    assert!(explanation.contains("case[1] -> n4.input : decision::Rejected"));
}

#[test]
fn missing_and_out_of_range_cases_are_rejected() {
    let (catalog, contract) = choice_catalog();
    let decision = contract.types["Decision"];
    let done = contract.types["Done"];
    let error = contract.types["DecisionError"];
    let mut builder = GraphBuilder::new("bad-choice", decision, done, vec![error]);
    let dispatch = builder.add_dispatch(decision);
    builder.add_terminal(TerminalKind::Success);
    builder.add_terminal(TerminalKind::KnownFailure);
    builder.add_terminal(TerminalKind::NotSent);
    builder.add_terminal(TerminalKind::Unknown);
    builder.connect(
        EdgeSource::WorkflowInput,
        EdgeTarget::DispatchInput(dispatch),
        None,
    );
    builder.connect(
        EdgeSource::DispatchCase(dispatch, 2),
        EdgeTarget::DispatchInput(dispatch),
        None,
    );
    let error = builder.finish(&catalog).unwrap_err();
    assert!(matches!(
        error.code,
        GraphErrorCode::InvalidEndpoint | GraphErrorCode::InvalidRoute
    ));
}
