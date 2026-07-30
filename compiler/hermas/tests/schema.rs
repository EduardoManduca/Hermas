use hermas::{Catalog, Compensation, Representation, compile_schema, generate_c_contract_header};

const COMPLETE_SCHEMA: &str = r#"
app warehouse

type Empty = record {}
type ItemId = String<32>
type Quantity = Integer
type Request = record {
    id: ItemId
    quantity: Quantity
}
type Requests = List<Request, 16>
type Failure = variant {
    unavailable: Request
    invalid: Empty
}
type Receipt = Bytes<64>

action release {
    input Receipt
    success Empty
    error Failure
    compensation none
}

action reserve {
    input Request
    success Receipt
    error Failure
    compensation release
}
"#;

#[test]
fn hash_is_the_only_line_comment_syntax() {
    let source = format!("# schema comment\n{COMPLETE_SCHEMA}");
    let mut catalog = Catalog::new();
    assert!(compile_schema(&mut catalog, "hash.hschema", &source).is_ok());

    let mut rejected_catalog = Catalog::new();
    let rejected = format!("// schema comment\n{COMPLETE_SCHEMA}");
    assert!(compile_schema(&mut rejected_catalog, "double-slash.hschema", &rejected).is_err());
}

#[test]
fn compiles_closed_contracts_and_compensation_capabilities() {
    let mut catalog = Catalog::new();
    let contract =
        compile_schema(&mut catalog, "warehouse.hschema", COMPLETE_SCHEMA).expect("valid schema");

    assert_eq!(catalog.app_name(contract.app), "warehouse");
    assert_eq!(contract.types.len(), 7);
    assert_eq!(contract.actions.len(), 2);
    assert_eq!(contract.fingerprint.to_string().len(), 64);

    let reserve = catalog
        .action(contract.action_id("reserve").expect("reserve ID"))
        .expect("reserve Action");
    assert_eq!(
        reserve.compensation,
        Compensation::Action(contract.action_id("release").expect("release ID"))
    );
}

#[test]
fn generates_deterministic_c_contract_identity() {
    let mut catalog = Catalog::new();
    let contract = compile_schema(&mut catalog, "warehouse.hschema", COMPLETE_SCHEMA).unwrap();
    let header = generate_c_contract_header(&contract, "warehouse").unwrap();
    assert!(header.starts_with("#ifndef WAREHOUSE_HERMAS_CONTRACT_H\n"));
    assert!(!header.contains("_TYPE_"));
    assert!(!header.contains("_ACTION_RESERVE_ID"));
    assert!(header.contains("#define WAREHOUSE_ACTION_RESERVE_FINGERPRINT"));
    assert!(header.ends_with("\n#endif\n"));
    assert_eq!(
        header,
        generate_c_contract_header(&contract, "warehouse").unwrap()
    );
    assert!(generate_c_contract_header(&contract, "9invalid").is_err());

    let collision = r#"
        app collision
        type Input = Unit
        type Error = Unit
        action foo-bar {
            input Input
            success Input
            error Error
            compensation none
        }
        action foo_bar {
            input Input
            success Input
            error Error
            compensation none
        }
    "#;
    let mut collision_catalog = Catalog::new();
    let collision = compile_schema(&mut collision_catalog, "collision.hschema", collision).unwrap();
    assert!(generate_c_contract_header(&collision, "collision").is_err());
}

#[test]
fn fingerprint_is_semantic_and_ignores_layout_and_declaration_order() {
    let reordered = r#"
        app warehouse
        type Receipt=Bytes<64>
        type Failure=variant{invalid:Empty unavailable:Request}
        type Requests=List<Request 16>
        type Request=record{quantity:Quantity id:ItemId}
        type Quantity=Integer
        type ItemId=String<32>
        type Empty=record{}
        action reserve{input Request success Receipt error Failure compensation release}
        action release{input Receipt success Empty error Failure compensation none}
    "#;
    let mut first_catalog = Catalog::new();
    let first = compile_schema(&mut first_catalog, "first.hschema", COMPLETE_SCHEMA).unwrap();
    let mut second_catalog = Catalog::new();
    let second = compile_schema(&mut second_catalog, "second.hschema", reordered).unwrap();
    assert_eq!(first.fingerprint, second.fingerprint);
    assert_eq!(first.action_fingerprints, second.action_fingerprints);
}

#[test]
fn action_fingerprint_ignores_unreferenced_types_and_actions() {
    let baseline = r#"
        app modular
        type Input = Integer
        type Output = String<16>
        type Failure = Unit
        action run {
            input Input
            success Output
            error Failure
            compensation none
        }
    "#;
    let extended = r#"
        app modular
        type Input = Integer
        type Output = String<16>
        type Failure = Unit
        type Unused = Bytes<128>
        action run {
            input Input
            success Output
            error Failure
            compensation none
        }
        action report {
            input Unused
            success Unused
            error Failure
            compensation none
        }
    "#;
    let mut baseline_catalog = Catalog::new();
    let baseline = compile_schema(&mut baseline_catalog, "baseline.hschema", baseline).unwrap();
    let mut extended_catalog = Catalog::new();
    let extended = compile_schema(&mut extended_catalog, "extended.hschema", extended).unwrap();
    assert_ne!(baseline.fingerprint, extended.fingerprint);
    assert_eq!(
        baseline.action_fingerprint("run"),
        extended.action_fingerprint("run")
    );
}

#[test]
fn resolves_records_lists_and_variants_for_directional_presentation() {
    let source = r#"
        app source
        type Id = String<16>
        type Payload = record { id: Id }
        type Batch = List<Payload, 4>
        type Outcome = variant { ok: Batch }
        type Error = Unit
        action read { input Error success Outcome error Error compensation none }
    "#;
    let destination = r#"
        app destination
        type Id = String<32>
        type Payload = record { id: Id }
        type Batch = List<Payload, 8>
        type Outcome = variant { ok: Batch }
        type Error = Unit
        action write { input Outcome success Error error Error compensation none }
    "#;
    let mut catalog = Catalog::new();
    let source = compile_schema(&mut catalog, "source.hschema", source).unwrap();
    let destination = compile_schema(&mut catalog, "destination.hschema", destination).unwrap();
    let narrow = source.type_id("Outcome").unwrap();
    let wide = destination.type_id("Outcome").unwrap();
    assert!(catalog.representation_compatible(narrow, wide).is_ok());
    assert!(catalog.representation_compatible(wide, narrow).is_err());
}

#[test]
fn reports_unknown_and_recursive_types_with_source_locations() {
    let mut catalog = Catalog::new();
    let unknown = compile_schema(
        &mut catalog,
        "unknown.hschema",
        "app bad\n\
         type Request = Missing\n\
         type Error = Unit\n\
         action run { input Request success Error error Error compensation none }",
    )
    .unwrap_err();
    assert_eq!(unknown.stage, "schema");
    assert_eq!(unknown.code, "unknown-type");
    assert_eq!(unknown.span.line, 2);

    let recursive = compile_schema(
        &mut catalog,
        "recursive.hschema",
        "app cycle\n\
         type Node = record { next: Node }\n\
         type Error = Unit\n\
         action run { input Node success Error error Error compensation none }",
    )
    .unwrap_err();
    assert_eq!(recursive.code, "recursive-representation");
}

#[test]
fn requires_explicit_compensation_capability_and_known_local_port_types() {
    let mut catalog = Catalog::new();
    let missing_compensation = compile_schema(
        &mut catalog,
        "kind.hschema",
        "app bad\n\
         type Empty = Unit\n\
         action run { input Empty success Empty error Empty }",
    )
    .unwrap_err();
    assert_eq!(missing_compensation.stage, "syntax");

    let legacy_kind = compile_schema(
        &mut catalog,
        "legacy-kind.hschema",
        "app legacy\n\
         type Empty = Unit\n\
         action run { input Empty success Empty error Empty kind irreversible }",
    )
    .unwrap_err();
    assert_eq!(legacy_kind.stage, "syntax");
    assert!(legacy_kind.message.contains("`compensation`"));

    let unknown_port = compile_schema(
        &mut catalog,
        "port.hschema",
        "app bad\n\
         type Empty = Unit\n\
         action run { input Missing success Empty error Empty compensation none }",
    )
    .unwrap_err();
    assert_eq!(unknown_port.code, "unknown-action-type");
}

#[test]
fn rejects_invalid_compensation_without_mutating_the_catalog() {
    let mut catalog = Catalog::new();
    let error = compile_schema(
        &mut catalog,
        "compensation.hschema",
        "app payments\n\
         type Request = Integer\n\
         type Token = Integer\n\
         type WrongToken = Boolean\n\
         type Done = Unit\n\
         type Failure = Unit\n\
         action undo { input WrongToken success Done error Failure compensation none }\n\
         action apply { input Request success Token error Failure compensation undo }",
    )
    .unwrap_err();
    assert_eq!(error.stage, "catalog");
    assert_eq!(error.code, "invalid-action");
    assert!(error.message.contains("source representation Integer"));

    let valid = compile_schema(
        &mut catalog,
        "fresh.hschema",
        "app payments\n\
         type Empty = Unit\n\
         action check { input Empty success Empty error Empty compensation none }",
    );
    assert!(valid.is_ok(), "failed compilation must be transactional");
}

#[test]
fn rejects_bad_bounds_and_empty_variants() {
    for (source, code) in [
        (
            "app bad type Value = List<Integer, 0> action run { input Value success Value error Value compensation none }",
            "invalid-type",
        ),
        (
            "app bad type Value = variant {} action run { input Value success Value error Value compensation none }",
            "empty-variant",
        ),
    ] {
        let mut catalog = Catalog::new();
        assert_eq!(
            compile_schema(&mut catalog, "bad.hschema", source)
                .unwrap_err()
                .code,
            code
        );
    }
}

#[test]
fn representation_wire_sizes_include_bounds_and_tags() {
    let mut fields = std::collections::BTreeMap::new();
    fields.insert("label".to_owned(), Representation::String { maximum: 12 });
    fields.insert("payload".to_owned(), Representation::Bytes { maximum: 20 });
    assert_eq!(Representation::Record(fields).maximum_wire_size(), Some(48));

    let mut cases = std::collections::BTreeMap::new();
    cases.insert("ok".to_owned(), Representation::Integer);
    cases.insert("none".to_owned(), Representation::Unit);
    assert_eq!(Representation::Variant(cases).maximum_wire_size(), Some(16));
}
