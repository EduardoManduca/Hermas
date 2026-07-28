# Hermas runtime workspace

Status: implemented Linux alpha operator convention.

A runtime workspace gives the daemon, Action processes, callers, and history
tool one directory name instead of requiring users to coordinate internal
socket and state paths:

```text
runtime/
├── apps.sock
├── control.sock
└── state/
    ├── journal.hj
    ├── results.hr
    ├── compensation.hc
    └── saga.hs
```

The socket names and state layout are fixed for the alpha. They are runtime
implementation details, not HScript resources or application manifests.

Start and use a workspace with:

```text
hermasd --workspace ./runtime workflow.hgi 1
my_action --workspace ./runtime ACTION_FINGERPRINT
hermas_run --workspace ./runtime 1 --image workflow.hgi --value 42
hermas_history --workspace ./runtime
```

`hermasd` creates the workspace and state directories with mode `0700` when
absent. Every tool rejects a directory that is a symlink, is not owned by the
effective user, grants any group or other permission, or would create a Unix
socket path that does not fit the platform bound. Existing paths are never
replaced. Durable files retain their independent `O_NOFOLLOW`, ownership,
permission, format, checksum, and lock validation.

The explicit daemon, edge, client, and journal paths remain supported for
embedding, isolated tests, and advanced operators. The workspace convention
does not change protocol frames, execution identities, delivery semantics, or
recovery classification.
