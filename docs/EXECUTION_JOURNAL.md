# Hermas2 Execution Journal

Status: durable delivery-fact journal, version 1.

The journal is mutable execution history. It is deliberately separate from
the immutable graph image and never contains application payloads. It is not
a queue, retry log, or source of application state.

## Record format

Every record is exactly 64 bytes and uses unsigned little-endian fields:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `H2JR` |
| 4 | 2 | Version `1` |
| 6 | 2 | Record size `64` |
| 8 | 2 | Record kind |
| 10 | 2 | Protocol outcome |
| 12 | 4 | Reserved, zero |
| 16 | 8 | Strictly contiguous sequence |
| 24 | 8 | Execution ID |
| 32 | 4 | Workflow ID |
| 36 | 8 | Request ID, or zero |
| 44 | 2 | Graph node ID, or zero |
| 46 | 2 | App ID, or zero |
| 48 | 2 | Action ID, or zero |
| 50 | 2 | Reserved, zero |
| 52 | 8 | Graph-image fingerprint |
| 60 | 4 | CRC-32 of bytes `0..59` |

The record kinds are `ExecutionStarted`, `DeliveryPrepared`,
`DeliverySent`, `ActionSucceeded`, `ActionFailed`, `ActionUnknown`, and
`ExecutionFinished`.

The checksum detects accidental corruption and truncation. The image
fingerprint detects history being interpreted against a different image; it
is not an authorization signature.

## Write-ahead boundary

The daemon synchronizes `ExecutionStarted` before admitting an execution.
For every invocation it then:

1. Forms and validates the invocation frame.
2. Appends and synchronizes `DeliveryPrepared`.
3. Makes the socket write visible to the app.
4. Appends `DeliverySent` after complete packet delivery.
5. Appends exactly one closing Action fact.
6. Appends `ExecutionFinished` at a terminal.

Therefore a crash can never turn a prepared or delivered Action into
retryable work. A failure to append or synchronize is fatal to loop progress.

## Startup classification

Startup validates the complete file before accepting writes:

- Every record has exact magic, version, size, reserved fields, and checksum.
- Sequence numbers begin at one and are contiguous.
- Per-execution transitions and request routes are consistent.
- At most sixteen executions are unfinished simultaneously.
- Truncated or corrupt files are refused.

An interrupted `DeliveryPrepared` or `DeliverySent` is closed with
`ActionUnknown`, followed by `ExecutionFinished(Unknown)`. An unfinished
execution without an open delivery is also closed as `Unknown`. Hermas2 never
replays or resumes the forward graph from journal facts.

The next execution ID is one greater than the largest durable ID.

## Linux durability and inspection

`hermas2_journal_file_open` acquires an exclusive writer lock, scans the
existing file, and initializes the next sequence. Every append uses a complete
record write followed by `fdatasync`.

`hermas2_journal_file_inspect` provides read-only validated traversal without
acquiring the writer lock. The `hermas2_history` executable prints one
tab-separated row per record:

```text
hermas2_history execution.h2journal
```

History inspection exposes orchestration facts only. Payload capture,
rotation, compaction, distributed replication, forward resumption, retries,
and saga compensation tokens are intentionally excluded.
