# Hermas runtime workspace

Status: implemented managed Linux alpha workspace.

A runtime workspace gives the daemon, Action processes, callers, and history
tool one directory name. It also pins the exact graph and format family that
owns its durable state:

```text
runtime/
|-- manifest.hwm
|-- workflow.hgi
|-- apps.sock
|-- control.sock
`-- state/
    |-- journal.hj
    |-- results.hr
    |-- compensation.hc
    `-- saga.hs
```

Initialize and start a workspace once with:

```text
hermasd --workspace ./runtime workflow.hgi 1
```

The daemon first checks the source image's file safety, format, and support in
the current daemon build. It then applies the same capability decision to the
exact bytes being bound, copies them into `workflow.hgi`, and durably creates
`manifest.hwm`. An unsupported graph cannot initialize or pin a workspace.
The manifest binds the workspace to the workflow ID, image fingerprint and
size, graph-image and protocol versions, and every current durable-state
format version.

Automation can perform the read-only part independently:

```text
hermasd --check-image workflow.hgi
```

This command creates no workspace, durable state, or socket. It exits `0` for
a supported image and `4` for a valid graph that requires unavailable daemon
capabilities.

Before selecting or compiling a workflow shape, automation can query
`hermasd --capabilities` for the versioned JSON description of supported
HScript flow families and fixed daemon limits. See
`DAEMON_CAPABILITIES_V1.md` for that contract.

All later processes derive that binding:

```text
hermasd --workspace ./runtime
my_action --workspace ./runtime
hermas_run --workspace ./runtime 2 --value 42
hermas_history --workspace ./runtime
```

An Action binary carries the semantic fingerprint generated from its HSchema.
Workspace startup finds that fingerprint in the graph image and returns its
app, Action, input, success, and error Type assignments. Operators do not copy
fingerprints or numeric IDs into command lines. Reordering schema inputs may
change graph-local assignments without requiring an Action binary rebuild.

The initialization command is idempotent only when the workflow ID and exact
image bytes are unchanged. It never replaces an existing managed image or
manifest. A different workflow, different image, unsupported format version,
malformed manifest, missing managed image, changed image, or unsafe file is
rejected before sockets open or durable execution state is interpreted.
An unbound workspace containing any durable state is also rejected; Hermas
does not silently adopt state created without a manifest.

`hermasd` creates the workspace and state directories with mode `0700`.
Managed files use mode `0600`. Every tool rejects a directory that is a
symlink, is not owned by the effective user, grants any group or other
permission, or would create a Unix socket path beyond the platform bound.
Managed files are regular, owner-only, non-symlink files. Durable state files
retain their independent ownership, permission, format, checksum, and lock
validation.

The manifest and image are published only after complete writes and `fsync`.
An interrupted initialization may leave an unpublished temporary file or a
complete managed image without a manifest. Repeating the same initialization
can finish the latter case; different bytes are refused.

The explicit daemon, edge, client, and journal paths remain supported for
embedding, isolated tests, and advanced diagnostics. The managed convention
does not change protocol frames, execution identities, delivery semantics, or
recovery classification. See `WORKSPACE_MANIFEST_V1.md` for the canonical
manifest layout.
