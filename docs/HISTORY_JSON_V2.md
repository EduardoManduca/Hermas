# Hermas history JSON Lines v2

Status: stable machine-readable view of a validated journal-v2 snapshot for
the pre-1.0 alpha.

`hermas_history --json` emits UTF-8 JSON Lines. Every object carries
`"format":"hermas-history-v2"`. Records preserve the journal order and use
decimal strings for 64-bit identifiers. A successful stream ends with exactly
one summary object; consumers must require both that summary and exit status
zero.

The summary contains `journal_version`, `record_count`, `next_execution_id`,
the optional managed-workspace binding, and `interrupted`. Each interrupted
execution contains its identity and an `open_deliveries` array. That array has
at most eight entries, each with `delivery_was_sent`, request, node, app, and
Action identity. An empty array means the execution was active between
deliveries when the snapshot was taken.

Example interrupted execution:

```json
{"execution_id":"41","workflow_id":7,"image_fingerprint":"123456789abcdef0","open_deliveries":[{"delivery_was_sent":true,"request_id":"9","node_id":2,"app_id":3,"action_id":4},{"delivery_was_sent":false,"request_id":"10","node_id":3,"app_id":5,"action_id":6}]}
```

The output contains orchestration facts, never Action payloads. Consumers must
reject unknown `format` values. They may ignore additional object members
added compatibly to v2, but must not infer missing delivery entries or replay
work from interrupted history.
