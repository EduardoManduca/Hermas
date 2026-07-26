use herma2::{Catalog, compile_hscript, compile_schema, decode_graph_image, encode_graph_image};

fn compile_fixture() -> (Catalog, herma2::VerifiedGraph) {
    let mut catalog = Catalog::new();
    compile_schema(
        &mut catalog,
        "payment.hschema2",
        include_str!("../../../apps/saga/payment.hschema2"),
    )
    .unwrap();
    compile_schema(
        &mut catalog,
        "shipping.hschema2",
        include_str!("../../../apps/saga/shipping.hschema2"),
    )
    .unwrap();
    let graph = compile_hscript(
        &catalog,
        "fulfill.hscript2",
        include_str!("../../../apps/saga/fulfill.hscript2"),
    )
    .unwrap();
    (catalog, graph)
}

#[test]
fn saga_lowers_to_ordered_compensation_records() {
    let (catalog, graph) = compile_fixture();
    let image = encode_graph_image(&graph, &catalog).unwrap();
    assert_eq!(decode_graph_image(&image).unwrap().region_count, 6);

    let regions = u32::from_le_bytes(image[72..76].try_into().unwrap()) as usize;
    for (index, ordinal) in [1u16, 2, 3].into_iter().enumerate() {
        let offset = regions + index * 32;
        assert_eq!(image[offset], 3);
        assert_eq!(
            u16::from_le_bytes(image[offset + 12..offset + 14].try_into().unwrap()),
            ordinal
        );
        assert_eq!(image[offset + 16], 4);
    }
}

#[test]
fn decoder_rejects_corrupt_saga_metadata() {
    let (catalog, graph) = compile_fixture();
    let image = encode_graph_image(&graph, &catalog).unwrap();
    let regions = u32::from_le_bytes(image[72..76].try_into().unwrap()) as usize;

    for field in [4usize, 6, 8, 12, 20, 22] {
        let mut corrupt = image.clone();
        corrupt[regions + field..regions + field + 2].fill(0);
        assert!(decode_graph_image(&corrupt).is_err(), "field {field}");
    }
}

#[test]
fn saga_rejects_irreversible_and_nonsequential_work() {
    let mut catalog = Catalog::new();
    compile_schema(
        &mut catalog,
        "payment.hschema2",
        include_str!("../../../apps/saga/payment.hschema2"),
    )
    .unwrap();
    let irreversible = r#"
workflow invalid(input: payment::Token) -> payment::Empty
errors { payment::Failure }
{
    saga { return input |> payment/release() }
}
"#;
    let error = compile_hscript(&catalog, "invalid.hscript2", irreversible).unwrap_err();
    assert_eq!(error.code, "irreversible-saga-action");

    let parallel = r#"
workflow invalid(input: payment::Request) -> payment::Token
errors { payment::Failure }
{
    saga {
        let results = all input {
            left = input |> payment/reserve()
            right = input |> payment/reserve()
        }
        return results.left |> as payment::Request |> payment/reserve()
    }
}
"#;
    let error = compile_hscript(&catalog, "parallel.hscript2", parallel).unwrap_err();
    assert_eq!(error.code, "non-sequential-saga", "{error:?}");
}
