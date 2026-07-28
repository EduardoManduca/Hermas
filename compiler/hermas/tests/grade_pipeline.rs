mod support;

use hermas::{
    ActionId, Catalog, CompensationDeclaration, EdgeSource, EdgeTarget, GraphBuilder,
    GraphErrorCode, Representation, TerminalKind,
};
use support::{build_grade_pipeline, declare_grade_list, declare_mean_calculator, declare_printer};

#[test]
fn grade_pipeline_is_verified_and_inspectable() {
    let (catalog, graph) = build_grade_pipeline().expect("grade graph is valid");
    assert_eq!(graph.name(), "grade_pipeline");
    assert_eq!(
        graph.resources(&catalog).to_string(),
        "actions=3 dispatches=0 forks=0 joins=0 deadline_regions=0 each_regions=0 terminals=4 edges=13 apps=3 max_concurrent_actions=1 max_payload_bytes=264"
    );

    let explanation = graph.explain(&catalog);
    assert!(explanation.contains("n1 action grade-list/get"));
    assert!(
        explanation.contains(
            "n1.success -> n2.input : grade-list::GradeList as mean-calculator::MeanInput"
        )
    );
    assert!(
        explanation
            .contains("n2.success -> n3.input : mean-calculator::Mean as printer::PrintInput")
    );
    assert!(explanation.contains("n1.not-sent -> n6"));
    assert!(explanation.contains("n1.unknown -> n7"));
    assert!(explanation.contains("n3.success -> n4"));

    let dot = graph.to_dot(&catalog);
    assert!(dot.starts_with("digraph \"grade_pipeline\""));
    assert!(dot.contains("grade-list/get"));
    assert!(dot.contains("as mean-calculator::MeanInput"));
}

#[test]
fn independently_owned_compatible_types_require_presentation() {
    let mut catalog = Catalog::new();
    let grades = declare_grade_list(&mut catalog).expect("grade contract");
    let mean = declare_mean_calculator(&mut catalog).expect("mean contract");
    let printer = declare_printer(&mut catalog).expect("printer contract");

    let mut graph = GraphBuilder::new(
        "missing_presentation",
        grades.empty,
        printer.printed,
        vec![grades.error, mean.error, printer.error],
    );
    let get = graph.add_action(grades.get);
    let calculate = graph.add_action(mean.calculate);
    let success = graph.add_terminal(TerminalKind::Success);
    let failure = graph.add_terminal(TerminalKind::KnownFailure);
    let not_sent = graph.add_terminal(TerminalKind::NotSent);
    let unknown = graph.add_terminal(TerminalKind::Unknown);
    graph.connect(
        EdgeSource::WorkflowInput,
        EdgeTarget::ActionInput(get),
        None,
    );
    graph.connect(
        EdgeSource::ActionSuccess(get),
        EdgeTarget::ActionInput(calculate),
        None,
    );
    graph.connect(
        EdgeSource::ActionError(get),
        EdgeTarget::Terminal(failure),
        None,
    );
    graph.connect(
        EdgeSource::ActionNotSent(get),
        EdgeTarget::Terminal(not_sent),
        None,
    );
    graph.connect(
        EdgeSource::ActionUnknown(get),
        EdgeTarget::Terminal(unknown),
        None,
    );
    graph.connect(
        EdgeSource::ActionSuccess(calculate),
        EdgeTarget::Terminal(success),
        Some(printer.printed),
    );
    graph.connect(
        EdgeSource::ActionError(calculate),
        EdgeTarget::Terminal(failure),
        None,
    );
    graph.connect(
        EdgeSource::ActionNotSent(calculate),
        EdgeTarget::Terminal(not_sent),
        None,
    );
    graph.connect(
        EdgeSource::ActionUnknown(calculate),
        EdgeTarget::Terminal(unknown),
        None,
    );

    let error = graph.finish(&catalog).expect_err("nominal swap must fail");
    assert_eq!(error.code, GraphErrorCode::NominalMismatch);
    assert_eq!(error.edge, Some(1));
}

#[test]
fn incompatible_presentation_is_rejected() {
    let mut catalog = Catalog::new();
    let grades = declare_grade_list(&mut catalog).expect("grade contract");
    let mean = declare_mean_calculator(&mut catalog).expect("mean contract");
    let printer = declare_printer(&mut catalog).expect("printer contract");
    let bad_app = catalog.declare_app("bad").expect("bad app");
    let bad_type = catalog
        .declare_type(bad_app, "BooleanInput", Representation::Boolean)
        .expect("bad type");

    let mut graph = GraphBuilder::new(
        "bad_presentation",
        grades.empty,
        printer.printed,
        vec![grades.error, mean.error, printer.error],
    );
    let get = graph.add_action(grades.get);
    let calculate = graph.add_action(mean.calculate);
    let success = graph.add_terminal(TerminalKind::Success);
    let failure = graph.add_terminal(TerminalKind::KnownFailure);
    let not_sent = graph.add_terminal(TerminalKind::NotSent);
    let unknown = graph.add_terminal(TerminalKind::Unknown);
    graph.connect(
        EdgeSource::WorkflowInput,
        EdgeTarget::ActionInput(get),
        None,
    );
    graph.connect(
        EdgeSource::ActionSuccess(get),
        EdgeTarget::ActionInput(calculate),
        Some(bad_type),
    );
    graph.connect(
        EdgeSource::ActionError(get),
        EdgeTarget::Terminal(failure),
        None,
    );
    graph.connect(
        EdgeSource::ActionNotSent(get),
        EdgeTarget::Terminal(not_sent),
        None,
    );
    graph.connect(
        EdgeSource::ActionUnknown(get),
        EdgeTarget::Terminal(unknown),
        None,
    );
    graph.connect(
        EdgeSource::ActionSuccess(calculate),
        EdgeTarget::Terminal(success),
        Some(printer.printed),
    );
    graph.connect(
        EdgeSource::ActionError(calculate),
        EdgeTarget::Terminal(failure),
        None,
    );
    graph.connect(
        EdgeSource::ActionNotSent(calculate),
        EdgeTarget::Terminal(not_sent),
        None,
    );
    graph.connect(
        EdgeSource::ActionUnknown(calculate),
        EdgeTarget::Terminal(unknown),
        None,
    );

    let error = graph
        .finish(&catalog)
        .expect_err("incompatible presentation must fail");
    assert_eq!(error.code, GraphErrorCode::RepresentationMismatch);
    assert_eq!(error.edge, Some(1));
}

#[test]
fn every_action_must_handle_delivery_unknown() {
    let mut catalog = Catalog::new();
    let grades = declare_grade_list(&mut catalog).expect("grade contract");

    let mut graph = GraphBuilder::new(
        "missing_unknown",
        grades.empty,
        grades.grades,
        vec![grades.error],
    );
    let get = graph.add_action(grades.get);
    let success = graph.add_terminal(TerminalKind::Success);
    let failure = graph.add_terminal(TerminalKind::KnownFailure);
    let not_sent = graph.add_terminal(TerminalKind::NotSent);
    graph.add_terminal(TerminalKind::Unknown);
    graph.connect(
        EdgeSource::WorkflowInput,
        EdgeTarget::ActionInput(get),
        None,
    );
    graph.connect(
        EdgeSource::ActionSuccess(get),
        EdgeTarget::Terminal(success),
        None,
    );
    graph.connect(
        EdgeSource::ActionError(get),
        EdgeTarget::Terminal(failure),
        None,
    );
    graph.connect(
        EdgeSource::ActionNotSent(get),
        EdgeTarget::Terminal(not_sent),
        None,
    );

    let error = graph
        .finish(&catalog)
        .expect_err("missing Unknown edge must fail");
    assert_eq!(error.code, GraphErrorCode::InvalidPortCardinality);
    assert_eq!(error.node, Some(get));
}

#[test]
fn catalog_rejects_duplicate_declarations_and_bad_bounds() {
    let mut catalog = Catalog::new();
    let app = catalog.declare_app("grades").expect("first app");
    assert!(catalog.declare_app("grades").is_err());
    catalog
        .declare_type(app, "IntegerValue", Representation::Integer)
        .expect("first type");
    assert!(
        catalog
            .declare_type(app, "IntegerValue", Representation::Integer)
            .is_err()
    );
    assert!(
        catalog
            .declare_type(
                app,
                "Unbounded",
                Representation::list(Representation::Integer, 0),
            )
            .is_err()
    );
}

#[test]
fn unknown_actions_are_rejected_before_edge_validation() {
    let mut catalog = Catalog::new();
    let grades = declare_grade_list(&mut catalog).expect("grade contract");
    let mut graph = GraphBuilder::new(
        "unknown_action",
        grades.empty,
        grades.grades,
        vec![grades.error],
    );
    let unknown_action = graph.add_action(ActionId::from_raw(999));
    graph.add_terminal(TerminalKind::Success);
    graph.add_terminal(TerminalKind::KnownFailure);
    graph.add_terminal(TerminalKind::NotSent);
    graph.add_terminal(TerminalKind::Unknown);

    let error = graph
        .finish(&catalog)
        .expect_err("unknown Action ID must fail");
    assert_eq!(error.code, GraphErrorCode::UnknownAction);
    assert_eq!(error.node, Some(unknown_action));
}

#[test]
fn every_terminal_kind_occurs_exactly_once() {
    let mut catalog = Catalog::new();
    let grades = declare_grade_list(&mut catalog).expect("grade contract");

    let mut missing = GraphBuilder::new(
        "missing_terminal",
        grades.empty,
        grades.grades,
        vec![grades.error],
    );
    missing.add_action(grades.get);
    missing.add_terminal(TerminalKind::Success);
    missing.add_terminal(TerminalKind::KnownFailure);
    missing.add_terminal(TerminalKind::NotSent);
    let error = missing
        .finish(&catalog)
        .expect_err("missing Unknown terminal must fail");
    assert_eq!(error.code, GraphErrorCode::MissingTerminal);

    let mut duplicate = GraphBuilder::new(
        "duplicate_terminal",
        grades.empty,
        grades.grades,
        vec![grades.error],
    );
    duplicate.add_action(grades.get);
    duplicate.add_terminal(TerminalKind::Success);
    duplicate.add_terminal(TerminalKind::KnownFailure);
    duplicate.add_terminal(TerminalKind::NotSent);
    duplicate.add_terminal(TerminalKind::Unknown);
    duplicate.add_terminal(TerminalKind::Unknown);
    let error = duplicate
        .finish(&catalog)
        .expect_err("duplicate Unknown terminal must fail");
    assert_eq!(error.code, GraphErrorCode::DuplicateTerminal);
}

#[test]
fn cycles_are_rejected_before_port_cardinality() {
    let mut catalog = Catalog::new();
    let app = catalog.declare_app("cycle-app").expect("cycle app");
    let token = catalog
        .declare_type(app, "Token", Representation::Unit)
        .expect("token type");
    let error_type = catalog
        .declare_type(app, "CycleError", Representation::Unit)
        .expect("error type");
    let step = catalog
        .declare_action(
            app,
            "step",
            token,
            token,
            error_type,
            CompensationDeclaration::None,
        )
        .expect("step Action");
    let mut graph = GraphBuilder::new("cycle", token, token, vec![error_type]);
    let first = graph.add_action(step);
    let second = graph.add_action(step);
    graph.add_terminal(TerminalKind::Success);
    let failure = graph.add_terminal(TerminalKind::KnownFailure);
    let not_sent = graph.add_terminal(TerminalKind::NotSent);
    let unknown = graph.add_terminal(TerminalKind::Unknown);
    graph.connect(
        EdgeSource::WorkflowInput,
        EdgeTarget::ActionInput(first),
        None,
    );
    graph.connect(
        EdgeSource::ActionSuccess(first),
        EdgeTarget::ActionInput(second),
        None,
    );
    graph.connect(
        EdgeSource::ActionSuccess(second),
        EdgeTarget::ActionInput(first),
        None,
    );
    for action in [first, second] {
        graph.connect(
            EdgeSource::ActionError(action),
            EdgeTarget::Terminal(failure),
            None,
        );
        graph.connect(
            EdgeSource::ActionNotSent(action),
            EdgeTarget::Terminal(not_sent),
            None,
        );
        graph.connect(
            EdgeSource::ActionUnknown(action),
            EdgeTarget::Terminal(unknown),
            None,
        );
    }
    let error = graph.finish(&catalog).expect_err("cycle must fail");
    assert_eq!(error.code, GraphErrorCode::Cycle);
}

#[test]
fn graph_node_limit_is_hard() {
    let mut catalog = Catalog::new();
    let grades = declare_grade_list(&mut catalog).expect("grade contract");
    let mut graph = GraphBuilder::new(
        "too_many_nodes",
        grades.empty,
        grades.grades,
        vec![grades.error],
    );
    for _ in 0..65 {
        graph.add_action(grades.get);
    }
    graph.add_terminal(TerminalKind::Success);
    graph.add_terminal(TerminalKind::KnownFailure);
    graph.add_terminal(TerminalKind::NotSent);
    graph.add_terminal(TerminalKind::Unknown);

    let error = graph.finish(&catalog).expect_err("node limit must fail");
    assert_eq!(error.code, GraphErrorCode::LimitExceeded);
}
