# Hermas fuzz targets

Fuzzing is a Linux release gate. Rust targets use `cargo-fuzz`:

```text
cargo install cargo-fuzz
tools/generate_test_fixtures.sh
cargo fuzz run image target/gate-fixtures -- -max_total_time=60
cargo fuzz run schema fuzz/seeds/schema -- -max_total_time=60
cargo fuzz run hscript fuzz/seeds/hscript -- -max_total_time=60
```

The C target multiplexes graph images, protocol packets, journals, terminal
results, compensation tokens, and saga-attempt logs:

```text
cmake -S . -B build-fuzz \
  -DCMAKE_C_COMPILER=clang \
  -DHERMAS2_SANITIZE=ON \
  -DHERMAS2_BUILD_FUZZERS=ON
cmake --build build-fuzz --target hermas2_c_fuzz
build-fuzz/hermas2_c_fuzz -max_total_time=60 CORPUS_DIR
```

Pull requests run bounded smoke sessions. Scheduled CI runs longer sessions.
Any crashing input is retained as a regression fixture before a fix merges.
The text-parser seeds are versioned under `fuzz/seeds`; image fuzzing starts
from every deterministic release-gate graph.

`tests/check_image_parity.py` generates a shared deterministic malformed-image
corpus from the valid gate fixtures and requires the independent Rust and C
decoders to make the same accept/reject decision.
