# Pre-1.0 compatibility policy

Hermas `0.1.x` is an experimental safety-complete alpha, not a stable ABI
promise.

- The original Hermas prototype is not source, ABI, protocol, image, grammar,
  or state compatible with this implementation.
- Every binary protocol and durable format carries an explicit version.
  Unsupported versions are rejected; they are never guessed or silently
  reinterpreted.
- Machine-readable CLI contracts carry their own format identifier.
  `hermas-history-v2` is a view of journal v2, not a replacement durable
  format, and `hermas-execution-result-v1` is a lossless view of one terminal
  caller frame. Incompatible output changes require a new identifier.
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

Journal v2 and `hermas-history-v2` introduce bounded concurrent open
deliveries. Journal v1 workspaces are intentionally rejected by this build;
there is no silent reinterpretation or in-place migration.
