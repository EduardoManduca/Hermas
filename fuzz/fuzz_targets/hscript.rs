#![no_main]

use hermas::{compile_hscript_module, Catalog};
use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    let source = String::from_utf8_lossy(data);
    let catalog = Catalog::new();
    let _ = compile_hscript_module(&catalog, "fuzz.hscript", &source);
});
