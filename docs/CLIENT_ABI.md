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
