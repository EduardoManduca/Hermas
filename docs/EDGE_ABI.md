# libhermas_edge C ABI

Status: implemented initial Linux caller-owned ABI.

`libhermas_edge` connects outward to the daemon over
`AF_UNIX` `SOCK_SEQPACKET`. It allocates no memory and owns no application
thread. The application owns:

- The `hermas_edge` context.
- Request packet storage.
- Result payload storage.
- Handler state.
- Its event-loop or blocking policy.

`hermas_edge_connect` registers the app ID and exact 32-byte semantic contract
fingerprint before accepting work. A mismatched registration is rejected by
the daemon rather than deferred until invocation.

The example Action processes accept `--workspace DIRECTORY`, derive the fixed
private app-registration socket through `hermas_workspace_open`, and resolve
the managed graph's complete Action contract from the compiler-generated
fingerprint embedded in that binary. App, Action, and port Type IDs come from
the validated graph and are not claimed by the process. Production bindings
may use the same helpers or call `hermas_edge_connect` with an explicit socket
supplied by their supervisor, but must still resolve and verify their compiled
identity.

`hermas_edge_serve_once`:

1. Receives exactly one packet with truncation detection.
2. Decodes and validates the protocol header.
3. Requires an `INVOKE` for the registered app.
4. Marks one complete delivery.
5. Calls the app-owned handler exactly once.
6. Requires exact success or app-error outcome metadata.
7. Encodes and sends one `RESULT`.

The low-level handler receives Action ID, destination input Type ID, and
canonical payload bytes. It returns an explicit result Type ID and outcome.
The example bootstrap checks those IDs against the resolved contract and
chooses its success or error Type from the outcome, keeping graph-local
numbers out of business handlers. The library does not interpret business
meaning, convert payloads, retry, supervise, or allocate.

The Linux integration test uses a real `SOCK_SEQPACKET` socket and separate
client/server processes. It proves registration, invocation, result framing,
and exactly one handler call under ASan and UBSan.
