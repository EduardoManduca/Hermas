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

Generate the C identity header beside the app:

```sh
hermas schema c-header example.hschema example.contract.h EXAMPLE
```

Include that generated header and initialize the app's expected fingerprint
from `EXAMPLE_ACTION_PROCESS_FINGERPRINT`. Regenerate and commit the header
whenever the schema changes. The graph image embeds the independently
compiled fingerprint and registration succeeds only when both agree. Numeric
app, Action, and Type IDs are graph-image assignments; the generated header
deliberately does not present them as stable contract identities.

## 2. Own the handler

Include `hermas/edge.h`, allocate a caller-owned `hermas_edge`, packet
buffer, and result buffer. The low-level ABI connects with graph-local IDs
and the semantic fingerprint, but an app must not embed those numeric IDs.

On Linux, `hermas/workspace_linux.h` can resolve the app socket from the same
private runtime directory used by the daemon. The helper also verifies the
managed workspace manifest and image before the app connects.
`hermas_workspace_find_action_contract` takes the generated fingerprint and
returns the installed app ID, Action ID, input Type, success Type, and error
Type. These are validated deployment outputs, not application configuration.
Apps may therefore accept one workspace path from their supervisor instead of
exposing socket naming, graph-version selection, or catalog numbering.

The handler must:

- Accept only its declared Action and input nominal Type; a shared bootstrap
  may enforce the resolved IDs before entering business code.
- Treat input bytes as the canonical representation documented by HSchema.
- Return either `HERMAS_OUTCOME_SUCCESS` with the success Type or
  `HERMAS_OUTCOME_APP_ERROR` with the error Type.
- Keep business validation, authorization, side effects, and internal retry
  policy inside the app.

The Grade Pipeline and Order Total apps are complete independent examples.
Their shared bootstrap resolves and enforces deployment IDs, then selects the
resolved success or error Type from the handler outcome. Business handlers
see canonical values and never graph-local numbers.

## 3. Understand delivery ownership

Hermas writes a durable `DeliveryPrepared` fact before sending. Before a
complete send, loss is `NotSent`. After complete delivery, loss is `Unknown`;
Hermas will not retry. An app therefore must not assume a missing response
means its handler was never invoked.

One endpoint is normally single-flight for one Action. A bounded `each` region
may have up to its declared number of item requests in flight on the item
Action's endpoint. Responses may complete in any order and are matched by
request ID, so an `each` Action must preserve the invocation identifiers in
every response. Different Actions, including Actions from the same app, use
distinct endpoints and may progress concurrently. App-internal concurrency
remains app-owned.

Use `hermas_edge_receive_invocation` and retain its
`hermas_edge_invocation` token when an Action needs more than one request in
flight. Copy any business input that must outlive the caller-owned packet
buffer, then reply with `hermas_edge_send_result`. These helpers preserve the
delivery identity and construct the protocol response; Action code does not
decode or encode Hermas frames. `hermas_edge_serve_once` remains the compact
single-request helper. For a sequential long-lived Action,
`hermas_edge_serve_many` reuses the same buffers and registered connection for
an explicit nonzero number of invocations; it stops on the first error and
does not retry.

## 4. Compensation

Inside an explicit `saga`, a successful Action value is also its opaque
compensation token. On a known later failure, Hermas may invoke declared
compensations in reverse dependency order. It never compensates an uncertain
forward or reverse delivery automatically.

See `HSCHEMA.md`, `EDGE_ABI.md`, `PROTOCOL_V1.md`, and
`CRASH_RECOVERY_MATRIX.md` for normative details.
