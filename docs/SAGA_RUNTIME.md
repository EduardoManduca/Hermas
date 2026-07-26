# Hermas2 Saga Recovery Runtime

Status: bounded recovery planner and reverse compensation executor.

`hermas2_saga_execution` reconstructs compensation work from three immutable
inputs: a verified graph image, the complete execution journal, and the
durable compensation-token log. It allocates no heap memory and admits at most
16 saga steps.

## Recovery boundary

Recovery validates both logs in full before inspecting one execution. A
compensation plan is eligible only when:

- The execution finished with a known app error or `NotSent`.
- Every preceding `ActionSucceeded` names the next dense saga step.
- Every successful step has exactly one token under its complete durable key.
- Token compensation route and nominal types exactly match the graph image.
- The token is a canonical value of the compensation input representation.

A successful execution produces no work. Any `Unknown`, missing token,
duplicate token, mismatched image/workflow identity, route mismatch, skipped
step, or malformed history is refused. `Unknown` is reported as unsafe rather
than converted into compensating work.

## Reverse executor

`hermas2_saga_prepare` selects the greatest confirmed forward ordinal, copies
its token into caller-owned storage, and emits an ordinary `INVOKE` frame for
the declared compensation Action. Request IDs continue above the greatest
forward request ID.

The state machine is:

```text
Ready -> Prepared -> Sent -> Ready | Complete
                  \-> Blocked(NotSent)
                           Sent \-> Blocked(Unknown | AppError)
```

Successful compensation removes exactly one step and exposes its predecessor.
The runtime validates result identity, outcome Type ID, and canonical payload
using the compensation success/error types embedded in the image. It never
retries a compensation or proceeds past an uncertain or failed compensation.

This layer reconstructs forward facts and executes a bounded reverse plan.
`hermas2_saga_reconcile` additionally applies a validated saga attempt log.
Durably successful reverse steps reduce the remaining ordinal and are never
replayed. A finished log remains complete or blocked according to its terminal
outcome. Any open prepared or sent compensation is converted to blocked
`Unknown`; recovery never guesses whether the app observed it.

The executor and journal remain separate primitives: orchestration code owns
the write-ahead calls around socket delivery, while reconciliation owns the
restart decision.

`hermas2_saga_driver` is the narrow composition layer for callers that want
those write-ahead calls enforced. It appends `Started` before exposing the
plan, `DeliveryPrepared` before an invocation can be sent, and the matching
sent/outcome facts around runtime transitions. A final success or blocked
outcome is closed with `Finished`. Any log-write failure is fatal to driver
progress; it never silently degrades to in-memory execution.

The driver does not own sockets, files, app registration, or the forward
executor. This keeps recovery policy testable without merging durability,
transport, and graph traversal into one daemon-specific state machine.
