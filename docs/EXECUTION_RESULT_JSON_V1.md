# Execution result JSON v1

`hermas_run --json` emits one UTF-8 JSON object after one successfully
completed caller protocol exchange:

```json
{"format":"hermas-execution-result-v1","execution_id":"42","outcome":"success","source_type":7,"destination_type":7,"value_hex":"0100"}
```

The fields are:

- `format`: exactly `hermas-execution-result-v1`.
- `execution_id`: the protocol execution identity as a decimal string.
- `outcome`: `success`, `app-error`, `not-sent`, or `unknown`.
- `source_type` and `destination_type`: the nominal 16-bit Type IDs from the
  terminal frame.
- `value_hex`: the complete canonical result payload as lowercase hexadecimal.

There is exactly one line and no human-oriented `display` field. Consumers
that need typed display values validate and interpret `value_hex` against the
pinned graph image. This keeps the projection lossless and prevents the CLI
from guessing composite layouts.

The projection changes neither protocol behavior nor exit status. Success is
`0`, AppError is `10`, NotSent is `11`, and Unknown is `12`. Argument,
workspace, transport, and protocol failures remain non-domain errors on
standard error and emit no JSON result.
