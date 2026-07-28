# Alpha release checklist

A release commit must have a clean tree and atomic reviewed history.

- Rust format, Clippy with warnings denied, tests, and documentation pass.
- All 24 C tests pass under GCC and Clang with ASan and UBSan.
- Rust/C image decoder parity passes over every deterministic fixture.
- Rust and C fuzz smoke sessions report no crash.
- Static analysis and dependency/security scans report no unresolved finding.
- Grade Pipeline and independent Order Total integration tests pass.
- Both restart proofs preserve the exact completed journal.
- Installation into an empty prefix contains headers, libraries, daemon,
  client, history and image tools, examples, documentation, and licenses.
- Two identical builds from the same commit produce the same checksums.
- Version strings and tag agree; incompatible changes are documented.
- No safety-critical issue remains unresolved.

Only after these checks should maintainers create the public repository,
enable required checks and private vulnerability reporting, tag the commit,
and publish source plus the checksummed Linux x86-64 developer archive.
