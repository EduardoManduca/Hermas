# Hermas2 Terminal Result State

Status: canonical durable terminal-value format, version 1.

The execution journal proves orchestration and delivery facts but deliberately
contains no application payloads. A separate terminal-result log preserves the
canonical value that a completed workflow may later return to its caller. It
does not prove that execution completed: a matching `ExecutionFinished` fact
and graph-image identity remain authoritative for that claim.

Every record has a 64-byte little-endian `H2RS` header followed by at most one
protocol payload. The header contains a contiguous log sequence, execution,
workflow, and graph-image identity, the terminal `Success` or `AppError`
outcome, source and destination nominal Type IDs, payload length, reserved
fields, and CRC-32 over the canonical header fields plus value bytes.

There is at most one record for an execution/workflow/image tuple. Duplicate
keys are an ambiguity error. The format excludes `NotSent` and `Unknown`
because those operational outcomes have no nominal value.

## Durability ordering

For a terminal known value:

1. Validate its outcome, nominal Type IDs, and canonical payload against the
   graph image.
2. Append and synchronize the terminal-result record.
3. Append and synchronize the matching terminal execution fact.
4. Expose the result, or begin any required saga compensation.

A result record without the matching terminal journal fact is an orphan and
does not authorize a client result. A terminal known-value fact without
exactly one matching result record is incomplete recovery state. Neither case
permits reconstructing or guessing application bytes.

## Linux storage

The Linux implementation accepts only a non-symlink regular file owned by the
current user, creates it with mode `0600`, and holds an exclusive nonblocking
writer lock. Startup maps and validates the complete log before initializing
the next sequence. Every append is fully written and followed by `fdatasync`;
exact lookup maps only for the duration of the copy into caller-owned memory.
