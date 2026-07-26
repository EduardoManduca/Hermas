# Hermas2 Saga Attempt Log

Status: canonical bounded compensation-attempt journal, version 1.

The forward execution journal proves which Actions succeeded. The compensation
token log preserves their opaque result values. The saga attempt log is the
third, separate durability boundary: it records progress while those confirmed
steps are compensated in reverse order.

Every record is a fixed 64-byte little-endian `H2SG` record with a contiguous
sequence, execution/workflow/image identity, compensation request route,
forward node, reverse ordinal, outcome, reserved-field checks, and CRC-32.

The transition sequence is:

```text
Started
  (DeliveryPrepared -> DeliverySent -> StepSucceeded)*
  -> Finished(Success)
```

A prepared delivery may close as `StepFailed(NotSent)`. A sent delivery may
close as `StepFailed(AppError)`. Either prepared or sent delivery may close as
`StepUnknown`. Failed or unknown steps require a matching terminal `Finished`
record and never expose the predecessor.

The scanner handles at most 16 active sagas without allocation. It proves
dense descending ordinals, exact route continuity, outcome/delivery
compatibility, global sequence continuity, and terminal agreement. Its restart
summary distinguishes resumable state after a durable success from open
prepared/sent deliveries. An open delivery is uncertainty and must never be
replayed.

The saga executor consumes this summary through `hermas2_saga_reconcile`.
It resumes at `next_ordinal` only when there is no open delivery or terminal
failure, advances request IDs above both forward and compensation history,
and leaves completed logs complete across repeated restart.

## Linux storage

`hermas2_saga_log_file_open` accepts only a non-symlink regular file owned by
the current user, uses mode `0600`, and takes an exclusive nonblocking writer
lock. Startup memory-maps and validates the complete history before
initializing the next sequence. Every append is fully written and followed by
`fdatasync`; reopen and explicit scans observe only validated records.
