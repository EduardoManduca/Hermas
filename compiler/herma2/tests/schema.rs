use herma2::{ActionKind, Catalog, Representation, compile_schema};

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
    kind irreversible
}

action reserve {
    input Request
    success Receipt
    error Failure
    kind reversible compensate release
}
"#;

#[test]
fn compiles_closed_contracts_and_reversible_actions() {
    let mut catalog = Catalog::new();
    let contract =
        compile_schema(&mut catalog, "warehouse.hschema2", COMPLETE_SCHEMA).expect("valid schema");

    assert_eq!(catalog.app_name(contract.app), "warehouse");
    assert_eq!(contract.types.len(), 7);
    assert_eq!(contract.actions.len(), 2);
    assert_eq!(contract.fingerprint.to_string().len(), 64);

    let reserve = catalog
        .action(contract.action_id("reserve").expect("reserve ID"))
        .expect("reserve Action");
    assert_eq!(
        reserve.kind,
        ActionKind::Reversible {
            compensation: contract.action_id("release").expect("release ID")
        }
    );
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
        action reserve{input Request success Receipt error Failure kind reversible compensate release}
        action release{input Receipt success Empty error Failure kind irreversible}
    "#;
    let mut first_catalog = Catalog::new();
    let first = compile_schema(&mut first_catalog, "first.hschema2", COMPLETE_SCHEMA).unwrap();
    let mut second_catalog = Catalog::new();
    let second = compile_schema(&mut second_catalog, "second.hschema2", reordered).unwrap();
    assert_eq!(first.fingerprint, second.fingerprint);
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
        action read { input Error success Outcome error Error kind irreversible }
    "#;
    let destination = r#"
        app destination
        type Id = String<32>
        type Payload = record { id: Id }
        type Batch = List<Payload, 8>
        type Outcome = variant { ok: Batch }
        type Error = Unit
        action write { input Outcome success Error error Error kind irreversible }
    "#;
    let mut catalog = Catalog::new();
    let source = compile_schema(&mut catalog, "source.hschema2", source).unwrap();
    let destination = compile_schema(&mut catalog, "destination.hschema2", destination).unwrap();
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
        "unknown.hschema2",
        "app bad\n\
         type Request = Missing\n\
         type Error = Unit\n\
         action run { input Request success Error error Error kind irreversible }",
    )
    .unwrap_err();
    assert_eq!(unknown.stage, "schema");
    assert_eq!(unknown.code, "unknown-type");
    assert_eq!(unknown.span.line, 2);

    let recursive = compile_schema(
        &mut catalog,
        "recursive.hschema2",
        "app cycle\n\
         type Node = record { next: Node }\n\
         type Error = Unit\n\
         action run { input Node success Error error Error kind irreversible }",
    )
    .unwrap_err();
    assert_eq!(recursive.code, "recursive-representation");
}

#[test]
fn requires_explicit_action_kind_and_known_local_port_types() {
    let mut catalog = Catalog::new();
    let missing_kind = compile_schema(
        &mut catalog,
        "kind.hschema2",
        "app bad\n\
         type Empty = Unit\n\
         action run { input Empty success Empty error Empty }",
    )
    .unwrap_err();
    assert_eq!(missing_kind.stage, "syntax");

    let unknown_port = compile_schema(
        &mut catalog,
        "port.hschema2",
        "app bad\n\
         type Empty = Unit\n\
         action run { input Missing success Empty error Empty kind irreversible }",
    )
    .unwrap_err();
    assert_eq!(unknown_port.code, "unknown-action-type");
}

#[test]
fn rejects_invalid_compensation_without_mutating_the_catalog() {
    let mut catalog = Catalog::new();
    let error = compile_schema(
        &mut catalog,
        "compensation.hschema2",
        "app payments\n\
         type Request = Integer\n\
         type Token = Integer\n\
         type WrongToken = Boolean\n\
         type Done = Unit\n\
         type Failure = Unit\n\
         action undo { input WrongToken success Done error Failure kind irreversible }\n\
         action apply { input Request success Token error Failure kind reversible compensate undo }",
    )
    .unwrap_err();
    assert_eq!(error.stage, "catalog");
    assert_eq!(error.code, "invalid-action");
    assert!(error.message.contains("source representation Integer"));

    let valid = compile_schema(
        &mut catalog,
        "fresh.hschema2",
        "app payments\n\
         type Empty = Unit\n\
         action check { input Empty success Empty error Empty kind irreversible }",
    );
    assert!(valid.is_ok(), "failed compilation must be transactional");
}

#[test]
fn rejects_bad_bounds_and_empty_variants() {
    for (source, code) in [
        (
            "app bad type Value = List<Integer, 0> action run { input Value success Value error Value kind irreversible }",
            "invalid-type",
        ),
        (
            "app bad type Value = variant {} action run { input Value success Value error Value kind irreversible }",
            "empty-variant",
        ),
    ] {
        let mut catalog = Catalog::new();
        assert_eq!(
            compile_schema(&mut catalog, "bad.hschema2", source)
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
