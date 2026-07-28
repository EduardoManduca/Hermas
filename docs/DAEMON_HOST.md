# Hermas Linux Daemon Host

Status: runnable host with conservative forward closure and safe saga resume.

`hermasd` composes one independently validated graph image, the bounded app
registry and registration server, the bounded caller control server, and four
separate durable stores. It does not parse HScript, load application code, or
interpret nominal values.

Run it as:

```text
hermasd IMAGE WORKFLOW_ID STATE_DIR APP_SOCKET CONTROL_SOCKET
```

The state directory is created with mode `0700` when absent and must remain a
non-symlink directory owned by the current effective user with no group or
other permission bits. It contains:

- `journal.hj` for execution and delivery facts.
- `results.hr` for exact terminal Success and AppError values.
- `compensation.hc` for opaque compensation tokens.
- `saga.hs` for reverse-attempt progress.

The graph image must be a non-symlink regular file with no group or other
write bits. Both public sockets are nonblocking `AF_UNIX` `SOCK_SEQPACKET`
listeners created with mode `0600`. Existing socket paths are never removed
at startup; this prevents one daemon invocation from silently replacing
another.

The control listener does not accept a caller until every app required by the
loaded image has completed exact fingerprint registration. Kernel-bounded
connection queuing absorbs harmless process startup ordering; an early caller
does not turn a temporarily absent app into a workflow-level `NotSent`.

Caller-supplied execution IDs are monotonic durable identities. On startup the
host restores the next admissible ID from the journal; it rejects any lower
historical ID and advances the floor after each successful admission. This
prevents a completed execution ID from being reused and making the next
journal scan ambiguous.

On startup, unfinished forward executions are closed as `Unknown`; the host
never replays a prepared or sent Action. Active reverse plans from the saga
attempt log are independently reconstructed from the terminal forward
journal, exact compensation tokens, terminal result store, and reverse
history. Only a planner-proven `Ready` saga is installed into the fresh
transport loop. It waits for all required Actions, resumes at the next
uncompensated ordinal, and is released after its durable terminal state.
Uncertain reverse delivery returns `recovery-required`; inconsistent identity,
tokens, results, or transitions fail startup as a state error.

SIGINT and SIGTERM stop the process, close every owned descriptor and mapping,
release state locks, and remove only socket paths successfully bound by that
process.
