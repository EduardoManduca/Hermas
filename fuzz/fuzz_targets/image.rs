#![no_main]

use herma2::decode_graph_image;
use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    let _ = decode_graph_image(data);
});
