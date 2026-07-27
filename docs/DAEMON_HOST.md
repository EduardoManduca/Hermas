# Hermas2 Linux Daemon Host

Status: runnable clean-start host.

`hermas2d` composes one independently validated graph image, the bounded app
registry and registration server, the bounded caller control server, and four
separate durable stores. It does not parse HScript2, load application code, or
interpret nominal values.

Run it as:

```text
hermas2d IMAGE WORKFLOW_ID STATE_DIR APP_SOCKET CONTROL_SOCKET
```

The state directory is created with mode `0700` when absent and must remain a
non-symlink directory owned by the current effective user with no group or
other permission bits. It contains:

- `journal.h2j` for execution and delivery facts.
- `results.h2r` for exact terminal Success and AppError values.
- `compensation.h2c` for opaque compensation tokens.
- `saga.h2s` for reverse-attempt progress.

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

The current host starts only when the execution journal has no interrupted
execution. It returns `recovery-required` instead of replaying, discarding, or
partially recovering unfinished work. Clean completed history is reopened and
continued normally. A later recovery-host milestone will compose the already
implemented saga recovery planner before relaxing this startup refusal.

SIGINT and SIGTERM stop the process, close every owned descriptor and mapping,
release state locks, and remove only socket paths successfully bound by that
process.
