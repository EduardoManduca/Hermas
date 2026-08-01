# Crash and Recovery Matrix

Status: normative alpha restart classification.

| Last durable boundary | Startup classification | Automatic work |
| --- | --- | --- |
| No `ExecutionStarted` | No execution exists | None |
| `ExecutionStarted` only | Forward `Unknown` | Append terminal `Unknown`; do not execute |
| Forward `DeliveryPrepared` | Forward `Unknown` | Do not send or replay |
| Forward `DeliverySent` | Forward `Unknown` | Do not replay |
| Forward Action outcome without terminal | Forward `Unknown` | Do not continue the graph |
| Terminal result stored, no `ExecutionFinished` | Forward `Unknown` | Preserve result record; do not expose it as a completed execution |
| `ExecutionFinished` | Forward terminal | No forward replay |
| Compensation token before forward success fact | Token is non-authoritative | Do not compensate from token presence alone |
| Saga `Started`, no reverse delivery | Safe reverse plan | Resume at highest uncompensated ordinal after all Actions register |
| Reverse `DeliveryPrepared` | Reverse `Unknown` | Refuse startup as `recovery-required` |
| Reverse `DeliverySent` | Reverse `Unknown` | Refuse startup as `recovery-required` |
| Reverse `StepSucceeded` | Confirmed reverse progress | Resume at predecessor; never replay the successful step |
| Reverse failure/unknown plus `Finished` | Reverse terminal | Do not resume |
| Clean shutdown | Re-scan all stores | Apply the same rules; shutdown itself grants no extra authority |

Every record append that authorizes a later transition is synchronized before
that transition is exposed. Corruption, truncation, sequence gaps, identity
mismatch, duplicate authority, unsafe permissions, or lock conflicts are
state errors rather than recovery hints.

Forward classifications apply independently to every bounded `all` branch and
bounded `each` item delivery identity. A restart never reconstructs or replays
an incomplete group; it closes the containing execution as `Unknown` exactly
once under the same rules as sequential forward delivery.
