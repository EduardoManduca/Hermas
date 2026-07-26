# libhermas2edge C ABI

Status: implemented initial Linux caller-owned ABI.

`libhermas2edge` connects outward to the daemon over
`AF_UNIX` `SOCK_SEQPACKET`. It allocates no memory and owns no application
thread. The application owns:

- The `hermas2_edge` context.
- Request packet storage.
- Result payload storage.
- Handler state.
- Its event-loop or blocking policy.

`hermas2_edge_connect` registers the app ID and exact 32-byte semantic contract
fingerprint before accepting work. A mismatched registration is rejected by
the daemon rather than deferred until invocation.

`hermas2_edge_serve_once`:

1. Receives exactly one packet with truncation detection.
2. Decodes and validates the protocol header.
3. Requires an `INVOKE` for the registered app.
4. Marks one complete delivery.
5. Calls the app-owned handler exactly once.
6. Requires exact success or app-error outcome metadata.
7. Encodes and sends one `RESULT`.

The handler receives only Action ID, destination input Type ID, and canonical
payload bytes. It returns an explicit result Type ID and outcome. The library
does not interpret business meaning, convert payloads, retry, supervise, or
allocate.

The Linux integration test uses a real `SOCK_SEQPACKET` socket and separate
client/server processes. It proves registration, invocation, result framing,
and exactly one handler call under ASan and UBSan.
