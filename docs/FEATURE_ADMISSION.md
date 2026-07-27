# Architecture and feature admission

Hermas is a verified local Action-graph kernel, not a general workflow
language or distributed application platform. The master architecture governs
component boundaries and the philosophy document governs design choices.

A core feature is admissible only when it:

- Strengthens or preserves explicit delivery uncertainty.
- Has fixed, inspectable memory, concurrency, payload, and durable-state
  bounds.
- Can be verified before execution or checked at one narrow trust boundary.
- Defines behavior for every crash point and refuses ambiguous recovery.
- Preserves app ownership of business policy and side effects.
- Adds no silent format fallback, implicit retry, or hidden coercion.
- Has a concise specification and executable adversarial invariants.

Prefer an ecosystem Action, binding, diagnostic, or tool when functionality
does not need daemon authority. New syntax must lower to the existing graph
model or justify a deliberately reviewed model extension.

Distributed consensus, remote delivery, dynamic unbounded graphs, package
registries, and graphical editors are separate projects or future design
proposals, not alpha kernel requirements.
