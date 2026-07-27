mod support;

use herma2::{
    Catalog, compile_hscript, compile_hscript_module, compile_hscript_workflow, decode_graph_image,
    encode_graph_image,
};
use support::{build_grade_pipeline, declare_grade_list, declare_mean_calculator, declare_printer};

fn grade_catalog() -> Catalog {
    let mut catalog = Catalog::new();
    declare_grade_list(&mut catalog).unwrap();
    declare_mean_calculator(&mut catalog).unwrap();
    declare_printer(&mut catalog).unwrap();
    catalog
}

#[test]
fn hash_is_the_only_line_comment_syntax() {
    let catalog = grade_catalog();
    let body = "\
workflow print(input: printer::PrintInput) -> printer::Printed
errors { printer::PrintError }
{ return input |> printer/print() }";
    let with_hash = format!("# workflow comment\n{body}");
    let with_double_slash = format!("// workflow comment\n{body}");

    assert!(compile_hscript(&catalog, "hash.hscript2", &with_hash).is_ok());
    assert!(compile_hscript(&catalog, "double-slash.hscript2", &with_double_slash).is_err());
}

#[test]
fn real_grade_pipeline_is_compiled_with_complete_provenance() {
    let (catalog, graph) = build_grade_pipeline().expect("real pipeline compiles");
    assert!(graph.has_complete_source_map());
    assert_eq!(
        graph.resources(&catalog).to_string(),
        "actions=3 dispatches=0 forks=0 joins=0 deadline_regions=0 each_regions=0 terminals=4 edges=13 apps=3 max_concurrent_actions=1 max_payload_bytes=264"
    );
    let source_map = graph.source_map();
    assert_eq!(
        source_map
            .lines()
            .filter(|line| line.starts_with("node "))
            .count(),
        7
    );
    assert_eq!(
        source_map
            .lines()
            .filter(|line| line.starts_with("edge "))
            .count(),
        13
    );
    assert!(source_map.contains("grade-pipeline.hscript2:8:18"));
}

#[test]
fn typed_workflow_input_flows_by_exact_nominal_identity() {
    let catalog = grade_catalog();
    let source = r#"
workflow print(input: printer::PrintInput) -> printer::Printed
errors { printer::PrintError }
{
    return input |> printer/print()
}
"#;
    let graph = compile_hscript(&catalog, "print.hscript2", source).expect("typed workflow");
    assert!(graph.has_complete_source_map());
    assert!(
        graph
            .explain(&catalog)
            .contains("input -> n1.input : printer::PrintInput")
    );
}

#[test]
fn compilation_is_deterministic() {
    let catalog = grade_catalog();
    let source = r#"
workflow mean(input: mean-calculator::MeanInput) -> mean-calculator::Mean
errors { mean-calculator::MeanError }
{
    return input |> mean-calculator/calculate()
}
"#;
    let first = compile_hscript(&catalog, "mean.hscript2", source).unwrap();
    let second = compile_hscript(&catalog, "mean.hscript2", source).unwrap();
    assert_eq!(first.explain(&catalog), second.explain(&catalog));
    assert_eq!(first.to_dot(&catalog), second.to_dot(&catalog));
    assert_eq!(first.source_map(), second.source_map());
}

#[test]
fn rejects_unknown_actions_at_the_invocation_span() {
    let catalog = grade_catalog();
    let error = compile_hscript(
        &catalog,
        "unknown.hscript2",
        "workflow bad() -> printer::Printed\n\
         errors { printer::PrintError }\n\
         { return printer/missing() }",
    )
    .unwrap_err();
    assert_eq!(error.stage, "resolution");
    assert_eq!(error.code, "unknown-action");
    assert_eq!(error.span.line, 3);
}

#[test]
fn requires_explicit_compatible_nominal_presentation() {
    let catalog = grade_catalog();
    let missing = compile_hscript(
        &catalog,
        "missing-as.hscript2",
        "workflow bad() -> mean-calculator::Mean\n\
         errors { grade-list::GradeError mean-calculator::MeanError }\n\
         { return grade-list/get() |> mean-calculator/calculate() }",
    )
    .unwrap_err();
    assert_eq!(missing.stage, "typing");
    assert_eq!(missing.code, "nominal-mismatch");

    let incompatible = compile_hscript(
        &catalog,
        "bad-as.hscript2",
        "workflow bad() -> printer::Printed\n\
         errors { grade-list::GradeError printer::PrintError }\n\
         { return grade-list/get() |> as printer::PrintInput |> printer/print() }",
    )
    .unwrap_err();
    assert_eq!(incompatible.code, "incompatible-presentation");
}

#[test]
fn presentation_must_name_the_immediate_action_input() {
    let catalog = grade_catalog();
    let error = compile_hscript(
        &catalog,
        "wrong-target.hscript2",
        "workflow bad() -> mean-calculator::Mean\n\
         errors { grade-list::GradeError mean-calculator::MeanError }\n\
         { return grade-list/get() |> as printer::PrintInput |> mean-calculator/calculate() }",
    )
    .unwrap_err();
    assert_eq!(error.code, "wrong-presentation-target");
}

#[test]
fn every_action_error_must_be_declared_by_exact_type() {
    let catalog = grade_catalog();
    let error = compile_hscript(
        &catalog,
        "error.hscript2",
        "workflow bad() -> printer::Printed\n\
         errors { grade-list::GradeError }\n\
         { return printer/print() }",
    )
    .unwrap_err();
    assert_eq!(error.stage, "workflow");
    assert_eq!(error.code, "unhandled-action-error");
    assert!(error.message.contains("printer::PrintError"));
}

#[test]
fn immutable_bindings_cannot_be_redeclared_or_consumed_twice() {
    let catalog = grade_catalog();
    let duplicate = compile_hscript(
        &catalog,
        "duplicate.hscript2",
        "workflow bad() -> mean-calculator::Mean\n\
         errors { grade-list::GradeError mean-calculator::MeanError }\n\
         {\n\
           let grades = grade-list/get()\n\
           let grades = grades |> as mean-calculator::MeanInput |> mean-calculator/calculate()\n\
           return grades |> mean-calculator/calculate()\n\
         }",
    )
    .unwrap_err();
    assert_eq!(duplicate.code, "duplicate-binding");

    let reused = compile_hscript(
        &catalog,
        "reused.hscript2",
        "workflow bad() -> mean-calculator::Mean\n\
         errors { grade-list::GradeError mean-calculator::MeanError }\n\
         {\n\
           let grades = grade-list/get()\n\
           let mean = grades |> as mean-calculator::MeanInput |> mean-calculator/calculate()\n\
           return grades |> as mean-calculator::MeanInput |> mean-calculator/calculate()\n\
         }",
    )
    .unwrap_err();
    assert_eq!(reused.code, "binding-already-consumed");
}

#[test]
fn returned_pipeline_must_match_the_workflow_success_nominally() {
    let catalog = grade_catalog();
    let error = compile_hscript(
        &catalog,
        "return.hscript2",
        "workflow bad(input: printer::PrintInput) -> mean-calculator::Mean\n\
         errors { printer::PrintError }\n\
         { return input |> printer/print() }",
    )
    .unwrap_err();
    assert_eq!(error.stage, "typing");
    assert_eq!(error.code, "wrong-success-type");
}

#[test]
fn duplicate_workflow_errors_are_rejected() {
    let catalog = grade_catalog();
    let error = compile_hscript(
        &catalog,
        "duplicates.hscript2",
        "workflow bad(input: printer::PrintInput) -> printer::Printed\n\
         errors { printer::PrintError printer::PrintError }\n\
         { return input |> printer/print() }",
    )
    .unwrap_err();
    assert_eq!(error.code, "duplicate-error");
}

const MULTI_WORKFLOW_MODULE: &str = r#"
module operations

workflow calculate(input: mean-calculator::MeanInput) -> mean-calculator::Mean
errors { mean-calculator::MeanError }
{
    return input |> mean-calculator/calculate()
}

workflow print(input: printer::PrintInput) -> printer::Printed
errors { printer::PrintError }
{
    return input |> printer/print()
}
"#;

#[test]
fn one_module_compiles_independent_addressable_workflow_graphs() {
    let catalog = grade_catalog();
    let module = compile_hscript_module(&catalog, "operations.hscript2", MULTI_WORKFLOW_MODULE)
        .expect("multi-workflow module compiles");
    assert_eq!(module.name(), Some("operations"));
    assert_eq!(module.workflows().len(), 2);
    assert_eq!(module.workflows()[0].name(), "operations::calculate");
    assert_eq!(module.workflows()[1].name(), "operations::print");
    assert_eq!(
        module.workflow("calculate").unwrap().name(),
        "operations::calculate"
    );
    assert_eq!(
        module.workflow("operations::print").unwrap().name(),
        "operations::print"
    );
    assert!(module.workflow("missing").is_none());

    for name in ["calculate", "print"] {
        let graph =
            compile_hscript_workflow(&catalog, "operations.hscript2", MULTI_WORKFLOW_MODULE, name)
                .expect("selected workflow compiles");
        let image = encode_graph_image(&graph, &catalog).expect("image encodes");
        let decoded = decode_graph_image(&image).expect("image decodes");
        assert_eq!(decoded.workflow_name, format!("operations::{name}"));
        assert_eq!(decoded.node_count, 5);
    }
}

#[test]
fn single_workflow_api_requires_selection_for_a_module() {
    let catalog = grade_catalog();
    let error =
        compile_hscript(&catalog, "operations.hscript2", MULTI_WORKFLOW_MODULE).unwrap_err();
    assert_eq!(error.stage, "module");
    assert_eq!(error.code, "workflow-selection-required");

    let error = compile_hscript_workflow(
        &catalog,
        "operations.hscript2",
        MULTI_WORKFLOW_MODULE,
        "missing",
    )
    .unwrap_err();
    assert_eq!(error.code, "unknown-workflow");
}

#[test]
fn module_rejects_duplicate_names_and_checks_every_workflow() {
    let catalog = grade_catalog();
    let duplicate = MULTI_WORKFLOW_MODULE.replace("workflow print", "workflow calculate");
    let error = compile_hscript_module(&catalog, "duplicate-module.hscript2", &duplicate)
        .err()
        .expect("duplicate workflow is rejected");
    assert_eq!(error.stage, "module");
    assert_eq!(error.code, "duplicate-workflow");

    let invalid = MULTI_WORKFLOW_MODULE.replace("printer/print()", "printer/missing()");
    let error =
        compile_hscript_workflow(&catalog, "invalid-module.hscript2", &invalid, "calculate")
            .unwrap_err();
    assert_eq!(error.stage, "resolution");
    assert_eq!(error.code, "unknown-action");
    assert!(error.span.line > 6);

    let unnamed = MULTI_WORKFLOW_MODULE.replacen("module operations", "", 1);
    let error = compile_hscript_module(&catalog, "unnamed-module.hscript2", &unnamed)
        .err()
        .expect("unnamed multi-workflow module is rejected");
    assert_eq!(error.stage, "module");
    assert_eq!(error.code, "missing-module-name");
}
