# Hermas Threat Model

Status: release-gate model for the single-host alpha.

## Security objective

Hermas must preserve memory safety, bounded resource use, contract identity,
at-most-once delivery, and honest uncertainty when every external byte stream
is malformed or interrupted. It does not claim that an authorized app is
honest about its domain behavior.

## Trust boundaries

- **Graph images are untrusted.** Rust produces them, but the allocation-free
  C decoder independently validates the complete byte layout before runtime
  use. Unsupported versions and noncanonical layouts are refused.
- **App and caller packets are untrusted.** `SOCK_SEQPACKET` preserves packet
  boundaries, not correctness. Every frame is decoded field-by-field and
  checked against its connection role, Action identity, nominal types, and
  canonical payload representation.
- **Durable files are untrusted after restart.** Journal, result,
  compensation, and saga records have separate formats, checksums, sequences,
  transition validators, ownership requirements, and exclusive writer locks.
  Files are never repaired or partially accepted.
- **Socket paths and the state directory are hostile filesystem names.** The
  host refuses symlinks, unsafe ownership or permissions, and pre-existing
  socket paths. It removes only paths it successfully bound.
- **Apps are authorized domain principals, not trusted runtime peers.** Exact
  per-Action fingerprints prevent one endpoint from registering for a
  different contract. Apps still own authorization, validation, side effects,
  retries, and the truth of their returned domain result.

## Protected guarantees

- A completely delivered Action is never automatically delivered again.
- Loss after delivery becomes `Unknown`, never a fabricated failure.
- Interrupted forward execution is closed `Unknown` without replay.
- Compensation resumes only when durable history proves no reverse request
  may already have been delivered.
- Graph fan-out, connections, executions, payloads, and durable active sets
  remain within compile-time or image-declared bounds.
- Compiler and daemon agreement is not assumed: both validate serialized
  structure independently.

## Denial of service

The alpha bounds in-memory slots and packet sizes but does not promise
fairness against a malicious process running as the same operating-system
user. Silent registration and caller peers consume only fixed slots.
Filesystem capacity, CPU scheduling, and repeated process launches remain
operator-controlled resources.

## Out of scope

- Distributed or network transport.
- Exactly-once external side effects.
- Confidentiality from processes running as the same user.
- App sandboxing, supervision, package authenticity, or supply-chain trust.
- Recovery of application domain truth after `Unknown`.
- Compatibility with the original Hermas prototype or unsupported alpha
  format versions.

Security defects include memory unsafety, unbounded growth, contract
confusion, replay after possible delivery, silent state reinterpretation, or
any transition that hides `Unknown`.
