# Sequential C Runtime

Status: implemented Milestone 5 execution kernel.

`hermas2_execution` is caller-owned mutable state for one traversal of a
validated graph image. The caller also supplies the value buffer and its
capacity. The executor allocates no memory and never changes graph-image
bytes.

The state machine is:

```text
start -> Ready -> Prepared -> Sent -> Ready | Complete
                         \-> NotSent
                                  Sent \-> Unknown
```

- `Ready` means an Action and its canonical input are available.
- `Prepared` means an `INVOKE` frame has been formed but complete delivery has
  not been established.
- `Sent` means the caller established complete packet delivery.
- Failure in `Prepared` follows the graph's `NotSent` edge.
- Loss after `Sent` follows the graph's `Unknown` edge.
- Only a matching `RESULT` with the exact execution, request, app, Action,
  outcome type, and canonical payload can advance a sent Action.

Every Action attempt receives a monotonically increasing nonzero request ID.
No transition retries an Action. Presentation changes the destination nominal
type in the next `INVOKE`; it does not rewrite payload bytes.

The executor validates the complete image and initial value at admission.
Every app result is structurally validated using the canonical representation
descriptors embedded in that image. A result is copied only into the
caller-provided value buffer and is rejected if it exceeds its capacity.

The focused runtime test traverses the complete three-Action Grade Pipeline,
checks both presentation edges, and covers success, known app error,
`NotSent`, `Unknown`, malformed values, and illegal state transitions under
ASan and UBSan.

## Required-app registry

The Linux daemon registry is also caller-owned and bounded to the graph-image
limit of 64 required apps. Initialization copies only app IDs and 32-byte
contract fingerprints into fixed slots. Registration accepts only an app
listed in the loaded image, requires its exact fingerprint, rejects duplicate
connections, and acknowledges the app before exposing its descriptor for
dispatch.

The original accept operation remains a blocking primitive for focused
integration fixtures. Production composition uses
`hermas2_registration_server`: a fixed 64-slot owner for accepted but not yet
registered app connections. It polls every pending descriptor without heap
allocation, validates one complete registration packet, rejects malformed,
unknown, mismatched, and duplicate apps independently, and publishes an app
descriptor to the registry only after the complete `REGISTER_OK` packet has
been sent. A connected client that sends nothing cannot block another app or
the execution loop.

The end-to-end test runs the orchestrator and three independently built app
executables over `AF_UNIX` `SOCK_SEQPACKET`. Grade List returns
`[70, 80, 90]`, Mean Calculator returns `80`, and Printer emits exactly
`Mean: 80`. The same test rejects a wrong contract fingerprint before running
the valid pipeline and proves each app handler received exactly one completely
delivered invocation.

## Bounded nonblocking loop

`hermas2_daemon_loop` advances up to 16 executions through one rotating
`poll` scheduler. It is caller-owned and contains all execution, canonical
value, and packet storage:

- 16 fixed execution slots.
- One 65,488-byte value buffer per slot.
- One 65,536-byte packet buffer per slot.
- No allocation, thread, process, retry queue, or dynamically constructed
  dependency object inside the loop.

Admission beyond 16 active executions fails with `capacity-exhausted`.
Completed executions retain their result until the caller explicitly releases
the slot.

Ready executions are prepared in rotating order. An unavailable app leaves
the prepared invocation waiting without claiming delivery. Each registered
app descriptor can be owned by only one execution at a time, so multiple
workflows targeting one app are serialized while workflows targeting
different apps may progress independently.

Every send and receive uses nonblocking socket operations:

- A complete `SOCK_SEQPACKET` send advances `Prepared` to `Sent`.
- A failed send follows `NotSent`, drops the failed app connection, and never
  retries.
- A disconnect, truncated packet, malformed result, or metadata/value
  mismatch after `Sent` follows `Unknown` and drops the app connection.
- A valid result releases the app before the graph successor is scheduled.

The transport suite uses real socket pairs to prove pre-delivery `NotSent`,
post-delivery `Unknown`, and same-app single-flight behavior. The Grade
Pipeline integration runs through this same poll loop rather than a separate
test-only orchestrator.

## Bounded flow arena

`hermas2_group_execution` extends the graph interpreter with eight
caller-owned flow slots. Fork copies the immutable canonical input into fixed
branch buffers; Join retains each result under its dense field tag and does
not expose used fields until every branch has completed.

The engine enforces one prepared or sent request per app within an execution.
Independent apps can be completely delivered concurrently. After a failure,
undelivered work is cut off while sent work is awaited. Deterministic outcome
precedence is `Unknown`, known app failure, `NotSent`, then success.

For a root deadline region, the daemon reads the relative millisecond budget
and explicitly calls the expiry transition. Expiry before delivery is
`NotSent`; expiry after delivery is `Unknown`. The runtime does not claim to
cancel already-delivered work.
