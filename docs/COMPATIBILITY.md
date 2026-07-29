# Pre-1.0 compatibility policy

Hermas `0.1.x` is an experimental safety-complete alpha, not a stable ABI
promise.

- The original Hermas prototype is not source, ABI, protocol, image, grammar,
  or state compatible with this implementation.
- Every binary protocol and durable format carries an explicit version.
  Unsupported versions are rejected; they are never guessed or silently
  reinterpreted.
- Managed workspaces bind one exact graph image and workflow ID to all current
  protocol and durable-state versions. Daemon, Action, caller, and history
  entry points validate this binding before using sockets or state.
- An alpha release may change HSchema, HScript, C APIs, protocol frames,
  graph images, or durable state when the change materially improves safety
  or coherence.
- Release notes identify every incompatible change. If durable formats
  change without a verified migration tool, users must start with a new state
  directory.
- A migration tool, when introduced, must validate the complete source state,
  produce a separate destination, and fail without modifying the source.
- Format compatibility and public API stability will be promised only by a
  later explicit milestone; version `1.0.0` is not implied by project size.

Within one format version, decoders remain strict and deterministic. Adding a
fallback that accepts ambiguous old bytes is a breaking safety regression.
Changing manifest fields by hand is not a migration.
