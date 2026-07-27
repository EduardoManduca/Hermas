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

## Forward enrollment in the daemon loop

The nonblocking daemon loop may attach a compensation-token writer. When a
saga Action returns a valid success, the loop resolves that node's immutable
SagaStep metadata and synchronizes the opaque token before appending
`ActionSucceeded`. Only after both operations return can the already-computed
successor become schedulable on the next poll iteration.

A saga image without attached token durability, a route/type mismatch, or any
token append failure is fatal to loop progress. Ordinary non-saga images do
not require or write compensation state. The loop remains transport owner;
the graph image remains compensation-route authority, and the token log
remains opaque-value authority.

## Live plan construction

`hermas2_saga_begin_live` constructs the same bounded reverse executor from
the daemon's already-durable forward enrollment request IDs. Token access is a
small lookup capability rather than a file or byte-buffer assumption. The
in-memory log adapter and Linux compensation file both implement that
capability.

Live construction revalidates the image, dense enrollment prefix, monotonic
request boundary, every exact token key, route metadata, and canonical input
value. It therefore does not trust transient daemon bookkeeping as proof of
success; that bookkeeping only identifies the journal/token facts to verify.

## Live reverse execution in the daemon

`hermas2_daemon_loop_attach_saga` attaches the token writer and lookup
capability together with the saga-attempt writer. After a known forward
failure (`AppError` or `NotSent`), the loop constructs the verified live plan
and drives its compensation invocations through the existing nonblocking app
transport. The forward executor is already terminal at this point; the saga
driver exclusively owns the slot's prepare/send/result transitions until the
reverse plan closes.

The original known failure remains private while compensation is active.
Successful reverse completion releases that original result unchanged.
Failure before a compensation send becomes `NotSent`; any sent, rejected, or
otherwise uncertain compensation becomes `Unknown`. The loop never reports
successful rollback when the durable delivery facts cannot prove it.

Forward journal records remain forward facts only. Reverse preparation,
delivery, and outcomes are written solely to the saga-attempt log, while
opaque compensation inputs continue to come solely from the token log. This
keeps the three durable authorities distinct even though one transport loop
executes both directions.

## Restart handoff

After `hermas2_saga_recover` and `hermas2_saga_reconcile` produce a safe
`Ready` executor, `hermas2_daemon_loop_resume_saga` installs it into a fresh
transport loop. The loop replaces any transient token buffer with its attached
lookup capability and starts the driver in resume mode, so no second `Started`
record is emitted. Durable reverse successes are skipped and only the next
uncompensated ordinal is delivered.

The execution journal deliberately contains no business payloads. When the
terminal-result authority is attached, the daemon synchronizes a validated
known terminal value before `ExecutionFinished`. Restart then requires the
exact execution/workflow/image key, original outcome, failed saga edge Type
IDs, and canonical payload before restoring the original `AppError`.

Without that authority, a restarted loop still finishes safe compensation but
its eventual client result is conservatively `Unknown` with no value. If the
authority is configured and the required value is missing or inconsistent,
resume fails instead of silently degrading. This is distinct from delivery
uncertainty: compensation itself may be fully proven while the pre-crash
terminal value is unavailable. The daemon never fabricates it from journal
metadata.

On Linux, `hermas2_saga_recover_files` performs this recovery directly from
the three already-open, exclusively locked durable files. It maps them
read-only for the bounded validation pass, reconciles the reverse attempt
history, then discards every mapping. A safely recovered executor is rebound
to `hermas2_compensation_file_lookup`, so later preparation reads the token
from the locked file and no startup mapping or caller buffer can become a
dangling value authority.
