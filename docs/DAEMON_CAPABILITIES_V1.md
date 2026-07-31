# Daemon capabilities v1

Status: implemented automation contract.

`hermasd --capabilities` writes one UTF-8 JSON object followed by a newline,
then exits without opening an image, workspace, durable file, or socket.

The v1 document is:

```json
{
  "format": "hermas-daemon-capabilities-v1",
  "hermas_version": "0.1.0-alpha.1",
  "graph_image_version": 1,
  "protocol_version": 1,
  "formats": {
    "journal": 2,
    "result": 1,
    "compensation": 1,
    "saga_log": 1,
    "workspace_manifest": 1
  },
  "limits": {
    "actions": 80,
    "active_executions": 16
  },
  "flows": {
    "action": true,
    "match": true,
    "within": true,
    "saga": true,
    "all": false,
    "each": false
  }
}
```

Flow names are HScript concepts. `action` covers ordinary dependency-ordered
Action invocation, `match` covers typed choice, `within` covers deadlines,
and `saga` covers explicit compensation. `all` and `each` are compiled and
validated elsewhere in Hermas but are not yet integrated with the production
daemon's durable scheduler.

This JSON object is diagnostic metadata, not a Hermas value or an alternative
contract language. It never enters an Action invocation or graph image and
does not define Types, ports, or canonical values. HSchema remains the sole
source language for typed Action contracts; HScript remains the sole source
language for workflow structure.

Consumers must require the exact `format` value before interpreting fields.
They may ignore additional object members added compatibly to v1. A change to
the meaning or type of an existing member requires a new format identifier.
The numerical limits are hard bounds for this daemon build, not available
capacity at the instant of the query.

This document describes static build capabilities. To decide whether one
specific graph can run, use `hermasd --check-image IMAGE`; that command also
applies graph validation and safe-file rules.
Automation may append `--json` to receive exactly one
`hermas-image-check-v1` object with `status` equal to `supported`,
`unsupported`, or `invalid`, plus the stable host-result `reason`. Exit codes
remain `0` for supported, `4` for unsupported, and `1` for invalid.
The `flows` object is emitted from the same supported-feature mask used by
image admission. Required graph features are derived by the validated image
API, so the scheduler and command-line tool do not maintain separate format
interpretations for this decision.
