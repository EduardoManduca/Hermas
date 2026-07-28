# Hermas Compensation State

Status: durable opaque compensation-token format, version 1.

The execution journal records delivery facts and never stores application
values. Explicit sagas additionally require the successful value returned by
each Action with a named compensation because that app-owned value is the input token for its
declared compensating Action.

Hermas stores those tokens in a separate append-only log. Token presence
alone never proves that a forward Action succeeded.

## Record format

Each record has a 72-byte canonical little-endian header followed immediately
by a token of at most `HERMAS_PROTOCOL_MAX_PAYLOAD_SIZE` bytes. The header
contains:

- Magic `HCT1`, version `1`, header and complete record sizes.
- Strictly contiguous log sequence.
- Execution, workflow, request, graph-node, and graph-image identity.
- Compensation app and Action IDs.
- Separate forward-output and compensation-input nominal Type IDs.
- Token length, zero reserved fields, and CRC-32 over header plus token.

The complete execution/workflow/request/node/image tuple is the lookup key.
Missing tokens are reported as absent and duplicate keys are rejected as
ambiguous durable state.

## Required ordering

For a successful Action with a named compensation inside a saga:

1. Validate the result and compensation route.
2. Append and synchronize the opaque token.
3. Append and synchronize `ActionSucceeded` in the execution journal.
4. Permit dependent forward work.

A token written before the journal success is an uncommitted orphan and is
ignored during recovery. A durable journal success without exactly one token
is a fatal recovery inconsistency. Neither condition permits guessing.

## Linux storage

The Linux implementation accepts only a non-symlink regular file owned by the
current effective user with no group or other permission bits. New files use
mode `0600` and an exclusive lock. Every record is fully written and followed
by `fdatasync`. Startup scans
the complete log and rejects malformed, corrupt, truncated, or noncontiguous
records. Exact lookup copies the token into caller-owned storage.

This layer deliberately does not schedule compensation. Saga graph metadata,
reverse dependency scheduling, and reconciliation boundaries remain separate
steps. In particular, `Unknown` never authorizes compensation.
