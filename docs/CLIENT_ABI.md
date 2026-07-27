# Hermas2 Caller Client ABI

Status: implemented initial Linux caller-owned ABI.

`libhermas2client` is the workflow-caller counterpart to
`libhermas2edge`. It connects to the daemon's control `AF_UNIX`
`SOCK_SEQPACKET` socket and performs one explicit execution exchange:

1. Encode and send one complete `EXECUTE` frame.
2. Receive exactly one packet.
3. Reject truncation or malformed protocol bytes.
4. Require an `EXECUTION_RESULT` with the same execution ID.

The library assigns no execution IDs, retries no execution, and interprets no
application value. Input and output storage remain caller-owned. The result
payload points into the packet buffer supplied to `hermas2_client_execute` and
remains valid until that buffer is reused.

Connection lifetime and higher-level concurrency belong to the caller. A
caller may create independent client objects when it wants parallel workflow
executions; the daemon's bounded control server remains the capacity
authority.

## Shell runner

`hermas2_run` exposes the same one-execution ABI for development and operator
use:

```text
hermas2_run CONTROL_SOCKET EXECUTION_ID INPUT_TYPE [INPUT_HEX]
```

It prints the exact terminal outcome, nominal source and destination Type IDs,
and canonical value bytes in lowercase hexadecimal. Exit status is `0` for
Success, `10` for AppError, `11` for NotSent, and `12` for Unknown. Transport,
protocol, and argument failures remain distinct non-domain failures.
