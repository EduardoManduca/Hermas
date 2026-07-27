# Hermas2 Control Plane

Status: implemented packet-to-execution boundary.

The control plane is the narrow adapter between caller protocol frames and the
bounded daemon execution loop. It does not schedule Actions, interpret the
graph, own application sockets, or assign execution identities.

`hermas2_control_submit` accepts exactly one complete, validated `EXECUTE`
packet and admits its execution ID, nominal input Type ID, and canonical
payload into the daemon loop. Duplicate IDs, invalid values, and exhausted
execution capacity are admission failures. Malformed, truncated, or
non-`EXECUTE` frames are protocol failures.

`hermas2_control_collect` exposes only a terminal daemon result and encodes it
as `EXECUTION_RESULT`. Active execution is an explicit non-error state.
Collection does not release the execution slot: the transport owner releases
only after it has completely sent the result. This preserves the caller-owned
value bytes across nonblocking send retries.

The adapter allocates no memory and retains no client state. Linux connection
ownership and multiplexing are a separate layer built on these operations.

## Linux connection ownership

`hermas2_control_server` owns at most 16 caller connections, matching the
daemon execution bound. Each connection may submit exactly one `EXECUTE`.
Malformed frames receive a best-effort `PROTOCOL_ERROR` and are closed.

Results use nonblocking atomic `SOCK_SEQPACKET` sends. The execution slot is
released only after the complete `EXECUTION_RESULT` packet is sent. If a
caller disconnects first, its execution remains tracked without a descriptor;
the server lets already-admitted work reach a terminal state and then releases
it without attempting delivery. Extra caller traffic after admission detaches
the caller rather than altering execution.

The current server composes its client poll set with the existing app loop
through a fixed 10-millisecond active quantum. This bounds result latency
without merging client connections into Action-delivery state. No correctness
decision depends on the quantum.
