#![no_main]

use hermas::{compile_schema, Catalog};
use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    let source = String::from_utf8_lossy(data);
    let mut catalog = Catalog::new();
    let _ = compile_schema(&mut catalog, "fuzz.hschema", &source);
});
