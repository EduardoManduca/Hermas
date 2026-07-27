# Hermas alpha quickstart

The supported alpha runtime is Linux x86-64. Install a C17 compiler, CMake
3.20 or newer, Rust, and Cargo, then run from the repository root:

```text
tools/quickstart.sh
```

The script builds the Rust compiler and C runtime, compiles the real Grade
Pipeline contracts and workflow, discovers its input Type and per-Action
fingerprints from the validated image, starts three independent apps and
`hermas2d`, executes the workflow, and prints:

```text
Mean: 80
```

It then stops the daemon, validates the durable journal with
`hermas2_history`, restarts against the same state, and proves completed work
is not replayed. All sockets and temporary state are private and removed at
the end.

To install C artifacts under a prefix:

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix "$HOME/.local"
```

Install the compiler directly from a checkout with:

```text
cargo install --path compiler/herma2
```

`hermas2_run CONTROL_SOCKET EXECUTION_ID --image IMAGE [INPUT_HEX]` derives
the nominal workflow input Type from the independently validated image.
Advanced callers may still pass an explicit Type ID.
