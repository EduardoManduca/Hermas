# Writing a Hermas Action app in C

An app publishes semantic operations; it does not embed workflow policy.

## 1. Declare the contract

An HSchema file owns nominal types and Actions:

```text
app example

type Input = Integer
type Output = Integer
type Failure = Unit

action process {
    input Input
    success Output
    error Failure
    compensation none
}
```

Every Action explicitly publishes either `compensation none` or the name of
another local Action that accepts its success value. This is a capability
declaration, not a claim that side effects are literally reversible.

Run `hermas schema check FILE` to obtain the semantic per-Action fingerprint.
The graph image embeds the same fingerprint for runtime registration.

## 2. Own the handler

Include `hermas/edge.h`, allocate a caller-owned `hermas_edge`, packet
buffer, and result buffer, and connect with the exact app ID, local Action ID,
and fingerprint. `hermas_edge_serve_once` validates one invocation before
calling the handler.

On Linux, `hermas/workspace_linux.h` can resolve the app socket from the same
private runtime directory used by the daemon. The helper also verifies the
managed workspace manifest and image before the app connects. Apps may
therefore accept one workspace path from their supervisor instead of exposing
socket naming or graph-version selection as application configuration.

The handler must:

- Accept only its declared Action and input nominal Type.
- Treat input bytes as the canonical representation documented by HSchema.
- Return either `HERMAS_OUTCOME_SUCCESS` with the success Type or
  `HERMAS_OUTCOME_APP_ERROR` with the error Type.
- Keep business validation, authorization, side effects, and internal retry
  policy inside the app.

The Grade Pipeline apps are complete small examples. They share connection
bootstrap code, never business handlers.

## 3. Understand delivery ownership

Hermas writes a durable `DeliveryPrepared` fact before sending. Before a
complete send, loss is `NotSent`. After complete delivery, loss is `Unknown`;
Hermas will not retry. An app therefore must not assume a missing response
means its handler was never invoked.

One endpoint is single-flight for one Action. Different Actions, including
Actions from the same app, use distinct endpoints and may progress
concurrently. App-internal concurrency remains app-owned.

## 4. Compensation

Inside an explicit `saga`, a successful Action value is also its opaque
compensation token. On a known later failure, Hermas may invoke declared
compensations in reverse dependency order. It never compensates an uncertain
forward or reverse delivery automatically.

See `HSCHEMA.md`, `EDGE_ABI.md`, `PROTOCOL_V1.md`, and
`CRASH_RECOVERY_MATRIX.md` for normative details.
