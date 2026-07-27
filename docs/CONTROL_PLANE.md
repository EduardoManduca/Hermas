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
