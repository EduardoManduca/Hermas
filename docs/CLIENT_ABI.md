# Hermas Caller Client ABI

Status: implemented initial Linux caller-owned ABI.

`libhermas_client` is the workflow-caller counterpart to
`libhermas_edge`. It connects to the daemon's control `AF_UNIX`
`SOCK_SEQPACKET` socket and performs one explicit execution exchange:

1. Encode and send one complete `EXECUTE` frame.
2. Receive exactly one packet.
3. Reject truncation or malformed protocol bytes.
4. Require an `EXECUTION_RESULT` with the same execution ID.

The library assigns no execution IDs, retries no execution, and interprets no
application value. Input and output storage remain caller-owned. The result
payload points into the packet buffer supplied to `hermas_client_execute` and
remains valid until that buffer is reused.

Connection lifetime and higher-level concurrency belong to the caller. A
caller may create independent client objects when it wants parallel workflow
executions; the daemon's bounded control server remains the capacity
authority.

## Shell runner

`hermas_run` exposes the same one-execution ABI for development and operator
use:

```text
hermas_run CONTROL_SOCKET EXECUTION_ID INPUT_TYPE [INPUT_HEX]
hermas_run CONTROL_SOCKET EXECUTION_ID --image IMAGE --value VALUE
hermas_run CONTROL_SOCKET EXECUTION_ID --image IMAGE --hex INPUT_HEX
hermas_run --workspace DIRECTORY EXECUTION_ID --image IMAGE --value VALUE
```

Workspace mode derives the private control socket, so ordinary callers do not
name or coordinate socket files. The explicit socket form remains the direct
ABI diagnostic interface.

Image mode derives the workflow input Type and accepts human-readable scalar
values: `unit`, decimal `Integer`, `true` or `false`, unquoted `String` text,
and `0x`-prefixed `Bytes`. Record, List, and variant input remains available
through `--hex`; the CLI never guesses a composite layout. Both forms are
validated against the image's canonical Type descriptor before any connection
or delivery attempt.

The runner always prints the exact terminal outcome, nominal source and
destination Type IDs, and canonical value bytes in lowercase hexadecimal.
When an image is present and the result is scalar, it also appends a
human-readable `display=` value. Exit status is `0` for Success, `10` for
AppError, `11` for NotSent, and `12` for Unknown. Transport, protocol, and
argument failures remain distinct non-domain failures.
