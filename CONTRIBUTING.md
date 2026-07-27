# Contributing to Hermas

Hermas welcomes focused contributions that preserve its small safety model.
Diagnostics, documentation, malformed corpora, ecosystem Actions, bindings,
and tooling are excellent starting points.

## Before opening a change

Use an issue for semantic changes or work that affects durable formats,
delivery classification, graph verification, resource bounds, or the C trust
boundary. Small fixes may go directly to a pull request.

A runtime-semantic proposal must include:

1. The violated or newly required invariant.
2. A specification update.
3. Worst-case memory, concurrency, and durable-state bounds.
4. Crash behavior before and after every new durable boundary.
5. Executable positive and adversarial tests.
6. Compatibility and migration consequences.

Features that merely move policy into the daemon, hide `Unknown`, introduce
unbounded work, or silently reinterpret old bytes will not be accepted.
The complete decision rubric is in
[`docs/FEATURE_ADMISSION.md`](docs/FEATURE_ADMISSION.md).

## Development checks

Run the commands in the README. Linux changes to the C runtime should also run
both sanitizer configurations and `tools/quickstart.sh`. Add malformed inputs
to the shared decoder corpus when changing a validator.

Keep commits atomic and explain why a change is safe, not only what it does.
Do not mix generated build artifacts with source changes. All compiler
warnings are errors.

## Pull requests

Pull requests must describe their invariant impact, testing, format changes,
and recovery behavior. Maintainers may ask for a smaller design before
reviewing implementation. By contributing, you agree that your contribution
is licensed under `MIT OR Apache-2.0`.
