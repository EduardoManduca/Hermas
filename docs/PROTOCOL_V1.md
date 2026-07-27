# Hermas2 Protocol Version 1

Status: implemented frame codec and malformed-frame tests.

Hermas2 uses `AF_UNIX` `SOCK_SEQPACKET` on Linux. One packet contains exactly
one frame. The wire format is independent from the C ABI; every integer is
serialized explicitly in little-endian order.

## Header

Every packet contains a 48-byte header followed by exactly `payload_length`
bytes. The maximum packet size is 65,536 bytes.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `H2P1` |
| 4 | 2 | Protocol version `1` |
| 6 | 2 | Frame kind |
| 8 | 4 | Flags, zero |
| 12 | 4 | Header size, `48` |
| 16 | 8 | Execution ID |
| 24 | 8 | Request ID |
| 32 | 2 | App ID |
| 34 | 2 | Action ID |
| 36 | 2 | Source nominal Type ID |
| 38 | 2 | Destination nominal Type ID |
| 40 | 2 | Outcome |
| 42 | 2 | Reserved, zero |
| 44 | 4 | Payload length |

Execution and request IDs are nonzero for Action traffic. A request ID names
one at-most-once invocation attempt within one execution.

## Frame kinds

- `REGISTER_APP` carries an app ID, the app's current local Action ID, and
  that Action's exact 32-byte HSchema2 semantic fingerprint.
- `REGISTER_OK` acknowledges both registered IDs.
- `INVOKE` carries one Action request and canonical input payload.
- `RESULT` carries either exact app success or exact app error.
- `PROTOCOL_ERROR` reports a rejected frame without pretending it is an app
  error.
- `EXECUTE` admits one workflow input.
- `EXECUTION_RESULT` reports success, app error, not-sent, or delivery
  uncertainty.

`Unknown` and `NotSent` execution results carry no nominal value or payload.
They remain operational outcomes rather than HSchema2 domain values.

Registration is one connection per Action, not one connection per app. The
daemon matches the app identity and Action fingerprint against the loaded
graph image. It may therefore accept a local Action ID different from the
historical ID stored in that image and translates IDs at the transport
boundary.

## Delivery rule

The edge library counts an invocation as delivered only after one complete
packet has been received and validated as the registered app's `INVOKE`
frame. It invokes the app handler once. If the result cannot subsequently be
sent, the daemon must classify the already-delivered request as `Unknown`; it
must never retry it automatically.

Malformed magic, versions, kinds, flags, reserved words, sizes, IDs, outcomes,
and kind-specific payload rules are rejected before dispatch.
