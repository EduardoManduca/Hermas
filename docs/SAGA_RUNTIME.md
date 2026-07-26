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
Durable recording and restart reconciliation of the compensation attempts
themselves is the next boundary; callers must not treat this in-memory state
as durable progress.
