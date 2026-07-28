# Hermas roadmap

## Core safety work

Core work protects the delivery, typing, boundedness, durability, and recovery
model. It has release-blocking priority:

- Expand crash injection and shared malformed corpora.
- Increase parser, decoder, and durable-scanner fuzz coverage.
- Improve diagnostics without changing outcome semantics.
- Specify and verify any future durable-state migration tool.
- Harden local filesystem and socket handling across supported Linux systems.
- Document and test stable API/format commitments before `1.0`.

Runtime-semantic changes require specifications and executable invariants.
Distributed transport, consensus, automatic replay after possible delivery,
and unbounded dynamic graphs are not incremental daemon features.

## Ecosystem contribution areas

These areas can grow independently around the small kernel:

- Application Actions and reusable HSchema contracts
- C API ergonomics and additional language bindings
- Diagnostics, graph inspection, editors, and developer tooling
- Adversarial fixtures, fuzz corpora, and platform testing
- Tutorials, examples, packaging, and operational documentation

Package registries, graphical editors, remote transport, and non-C bindings
are intentionally outside the `0.1.0-alpha` release gate.
