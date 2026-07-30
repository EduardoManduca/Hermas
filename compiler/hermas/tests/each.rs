use hermas::{Catalog, compile_hscript, compile_schema, decode_graph_image, encode_graph_image};

fn catalog() -> Catalog {
    let mut catalog = Catalog::new();
    for (file, source) in [
        (
            "orders.hschema",
            r#"
app orders
type Trigger = Unit
type Order = Integer
type Orders = List<Order, 4>
type OrdersError = Unit
action list {
    input Trigger
    success Orders
    error OrdersError
    compensation none
}
"#,
        ),
        (
            "reporting.hschema",
            r#"
app reporting
type Input = Integer
type Report = Integer
type Reports = List<Report, 4>
type SmallReports = List<Report, 2>
type ReportError = Unit
action create {
    input Input
    success Report
    error ReportError
    compensation none
}
"#,
        ),
        (
            "archive.hschema",
            r#"
app archive
type Report = Integer
type Reports = List<Report, 4>
type Done = Boolean
type ArchiveError = Unit
action store {
    input Reports
    success Done
    error ArchiveError
    compensation none
}
"#,
        ),
    ] {
        compile_schema(&mut catalog, file, source).unwrap();
    }
    catalog
}

const WORKFLOW: &str = r#"
workflow batch(input: orders::Trigger) -> archive::Done
errors { orders::OrdersError reporting::ReportError archive::ArchiveError }
{
    let orders = input |> orders/list()
    let reports = each order in orders concurrency 3 {
        order |> as reporting::Input |> reporting/create()
    } collect as reporting::Reports
    return reports |> as archive::Reports |> archive/store()
}
"#;

#[test]
fn bounded_each_lowers_to_a_typed_ordered_collection_region() {
    let catalog = catalog();
    let graph = compile_hscript(&catalog, "batch.hscript", WORKFLOW).unwrap();
    assert_eq!(
        graph.resources(&catalog).to_string(),
        "actions=3 dispatches=0 forks=0 joins=0 deadline_regions=0 each_regions=1 terminals=4 edges=15 apps=3 max_concurrent_actions=3 max_payload_bytes=40"
    );
    let explanation = graph.explain(&catalog);
    assert!(explanation.contains("each[1].item"));
    assert!(explanation.contains("each[1].output"));
    let plan = graph.execution_plan(&catalog);
    assert!(plan.contains("each[1] template=n2 bound=4 concurrency=3 collect-order=source-index"));
    assert!(plan.contains("n2 action reporting/create waits=[n1, each-item]"));
    assert!(plan.contains("n3 action archive/store waits=[n2]"));

    let image = encode_graph_image(&graph, &catalog).unwrap();
    let decoded = decode_graph_image(&image).unwrap();
    assert_eq!(decoded.region_count, 1);
    let regions = u32::from_le_bytes(image[72..76].try_into().unwrap()) as usize;
    assert_eq!(image[regions], 2);
    assert_eq!(image[regions + 1], 3);

    let mut zero_concurrency = image.clone();
    zero_concurrency[regions + 1] = 0;
    assert!(decode_graph_image(&zero_concurrency).is_err());

    let mut wrong_bound = image;
    wrong_bound[regions + 12..regions + 14].copy_from_slice(&5u16.to_le_bytes());
    assert!(decode_graph_image(&wrong_bound).is_err());
}

#[test]
fn bounded_each_rejects_invalid_concurrency_and_collect_shape() {
    let catalog = catalog();
    let excessive = WORKFLOW.replace("concurrency 3", "concurrency 5");
    let error = compile_hscript(&catalog, "excessive.hscript", &excessive).unwrap_err();
    assert_eq!(error.code, "invalid-each-concurrency");

    let small = WORKFLOW.replace(
        "reporting::Reports\n    return",
        "reporting::SmallReports\n    return",
    );
    let error = compile_hscript(&catalog, "small.hscript", &small).unwrap_err();
    assert_eq!(error.code, "incompatible-collect");
}

#[test]
fn bounded_each_initial_template_is_exactly_one_action() {
    let catalog = catalog();
    let two_actions = WORKFLOW.replace(
        "|> reporting/create()\n    }",
        "|> reporting/create()\n            |> reporting/create()\n    }",
    );
    let error = compile_hscript(&catalog, "two-actions.hscript", &two_actions).unwrap_err();
    assert!(
        matches!(error.code, "nominal-mismatch" | "each-template-shape"),
        "{error}"
    );
}
