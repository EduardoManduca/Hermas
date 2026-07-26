use herma2::{
    Catalog, compile_hscript, declare_grade_list, declare_mean_calculator, decode_graph_image,
    encode_graph_image,
};

fn catalog() -> Catalog {
    let mut catalog = Catalog::new();
    declare_grade_list(&mut catalog).unwrap();
    declare_mean_calculator(&mut catalog).unwrap();
    catalog
}

#[test]
fn nested_within_records_a_parented_terminal_subgraph() {
    let catalog = catalog();
    let source = r#"
workflow nested(input: grade-list::Empty) -> mean-calculator::Mean
errors { grade-list::GradeError mean-calculator::MeanError }
{
    within 5s {
        let grades = input |> grade-list/get()
        within 1s {
            return grades
                |> as mean-calculator::MeanInput
                |> mean-calculator/calculate()
        }
    }
}
"#;
    let graph = compile_hscript(&catalog, "nested.hscript2", source).unwrap();
    assert_eq!(graph.resources(&catalog).deadline_regions, 2);
    let image = encode_graph_image(&graph, &catalog).unwrap();
    let decoded = decode_graph_image(&image).unwrap();
    assert_eq!(decoded.region_count, 2);
    let regions = u32::from_le_bytes(image[72..76].try_into().unwrap()) as usize;
    let nested = regions + 16;
    assert_eq!(
        u16::from_le_bytes(image[nested + 2..nested + 4].try_into().unwrap()),
        2
    );
    assert_eq!(
        u16::from_le_bytes(image[nested + 4..nested + 6].try_into().unwrap()),
        1
    );
    assert_eq!(
        u16::from_le_bytes(image[nested + 6..nested + 8].try_into().unwrap()),
        1
    );
    assert_eq!(
        u64::from_le_bytes(image[nested + 8..nested + 16].try_into().unwrap()),
        1000
    );

    let mut bad_parent = image;
    bad_parent[nested + 6..nested + 8].copy_from_slice(&2u16.to_le_bytes());
    assert!(decode_graph_image(&bad_parent).is_err());
}

const WITHIN: &str = r#"
workflow timed(input: mean-calculator::MeanInput) -> mean-calculator::Mean
errors { mean-calculator::MeanError }
{
    within 5s {
        return input |> mean-calculator/calculate()
    }
}
"#;

#[test]
fn root_within_lowers_to_one_deadline_region() {
    let catalog = catalog();
    let graph = compile_hscript(&catalog, "timed.hscript2", WITHIN).unwrap();
    assert_eq!(
        graph.resources(&catalog).to_string(),
        "actions=1 dispatches=0 forks=0 joins=0 deadline_regions=1 each_regions=0 terminals=4 edges=5 apps=1 max_concurrent_actions=1 max_payload_bytes=264"
    );
    let image = encode_graph_image(&graph, &catalog).unwrap();
    let decoded = decode_graph_image(&image).unwrap();
    assert_eq!(decoded.region_count, 1);
    let regions = u32::from_le_bytes(image[72..76].try_into().unwrap()) as usize;
    assert_eq!(
        u64::from_le_bytes(image[regions + 8..regions + 16].try_into().unwrap()),
        5000
    );

    let mut zero = image;
    zero[regions + 8..regions + 16].fill(0);
    assert!(decode_graph_image(&zero).is_err());
}

#[test]
fn root_within_requires_a_nonzero_bounded_duration() {
    let catalog = catalog();
    let zero = WITHIN.replace("5s", "0s");
    let error = compile_hscript(&catalog, "zero.hscript2", &zero).unwrap_err();
    assert_eq!(error.stage, "syntax");

    let unitless = WITHIN.replace("5s", "5");
    let error = compile_hscript(&catalog, "unitless.hscript2", &unitless).unwrap_err();
    assert_eq!(error.code, "invalid-duration");
}
