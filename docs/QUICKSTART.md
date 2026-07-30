# Hermas alpha quickstart

The supported alpha runtime is Linux x86-64. Install a C17 compiler, CMake
3.20 or newer, Rust, and Cargo, then run from the repository root:

```text
tools/quickstart.sh
```

The script builds the Rust compiler and C runtime, compiles the real Grade
Pipeline contracts and workflow, starts three independent apps and
`hermasd`, executes the workflow, and prints:

```text
Mean: 80
```

It then stops the daemon, validates the durable journal with
`hermas_history`, restarts against the same state, and proves completed work
is not replayed. All sockets and temporary state are private and removed at
the end. The daemon, apps, caller, and history tool share one `--workspace`
directory; users do not construct its internal socket paths.
The first daemon start pins the image and workflow ID into the workspace.
The caller and restart then derive the image, workflow, socket paths, and
format compatibility from that binding.

To install C artifacts under a prefix:

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix "$HOME/.local"
```

Install the compiler directly from a checkout with:

```text
cargo install --path compiler/hermas
```

`hermas_run --workspace DIRECTORY EXECUTION_ID --value VALUE` derives the
nominal workflow input Type from the independently validated managed image and
encodes scalar HSchema values without manual wire bytes. For example, the
Order Total test passes `--value 10000`. Exact canonical bytes remain
available through `--hex`; advanced callers may still pass an explicit image,
socket, and Type ID.

## Independent architecture check

Run `tools/test_order_total.sh` to compile and execute a second, unrelated
workflow. It accepts a caller-provided 10,000-cent subtotal, invokes discount,
tax, and receipt Actions, and prints:

```text
Order total: 9900 cents
```

This test has its own HSchema contracts, HScript graph, C processes, durable
state, and restart proof. Its schemas are intentionally supplied in a
different order from its execution pipeline, proving that Action binaries
resolve graph-local IDs and Types by semantic fingerprint rather than
embedding catalog numbers. It guards against accidentally specializing the
runtime or tooling to the Grade Pipeline.

`tools/test_workspace_compatibility.sh` independently verifies the managed
boundary. It refuses a changed workflow ID, a different graph image, and an
unsupported manifest version across daemon, history, and caller entry points.
