# Hermas history JSON Lines v1

Status: stable machine-readable view of a validated journal snapshot for the
pre-1.0 alpha.

`hermas_history --json` emits UTF-8 JSON Lines. Each line is one complete JSON
object and carries `"format":"hermas-history-v1"`. The command accepts either
a private journal path or a managed workspace:

```text
hermas_history --json execution.hj
hermas_history --json --workspace ./runtime
```

The inspector fixes the file length, validates the complete mapped snapshot,
and only then invokes the output visitor. Appends after that fixed length
belong to a later inspection. A successful stream ends with exactly one
`summary` object. Consumers must require exit status zero and that terminal
summary; output from a failed or interrupted process is incomplete and must be
discarded.

## Record object

Each durable record is emitted in sequence order:

```json
{"format":"hermas-history-v1","type":"record","sequence":"5","execution_id":"1","workflow_id":1,"kind":"action-succeeded","outcome":"success","request_id":"1","node_id":1,"app_id":1,"action_id":1,"image_fingerprint":"0123456789abcdef"}
```

The `kind` names are:

- `execution-started`
- `delivery-prepared`
- `delivery-sent`
- `action-succeeded`
- `action-failed`
- `action-unknown`
- `execution-finished`

The `outcome` names are `none`, `success`, `app-error`, `not-sent`, and
`unknown`. Non-Action records use JSON `null` for request, node, app, and
Action IDs.

Every unsigned 64-bit integer is a decimal string. Image fingerprints are
fixed-width lowercase hexadecimal strings. This prevents precision loss in
JSON implementations whose numeric type cannot exactly represent all 64-bit
integers. Workflow, node, app, and Action IDs remain JSON numbers because
their formats are bounded below the exact-integer limits of common parsers.

## Summary object

The final object contains the validated snapshot classification:

```json
{"format":"hermas-history-v1","type":"summary","journal_version":1,"record_count":"5","next_execution_id":"2","workspace":{"manifest_version":1,"workflow_id":1,"image_fingerprint":"0123456789abcdef"},"interrupted":[]}
```

`workspace` is `null` for direct-file inspection. In managed mode it repeats
the independently validated workspace binding. `interrupted` contains at most
the journal's fixed unfinished-execution bound. Each entry carries execution,
workflow, image, and delivery-route facts plus boolean `has_open_delivery` and
`delivery_was_sent`.

An interrupted entry is not permission to replay work. Forward restart remains
conservative: the daemon closes interrupted forward execution as `Unknown`.
Saga compensation still requires the separate recovery planner and its
durable compensation state. Agents should use this output to observe Hermas
facts, never to invent a weaker recovery rule.

## Compatibility

Field meanings, names, nullability, and integer encodings are part of
`hermas-history-v1`. Additive fields require consumers to ignore unknown keys.
Removing or reinterpreting a field, changing a string into a JSON number, or
changing recovery meaning requires a new format identifier. The JSON view
does not version or replace the binary journal.
