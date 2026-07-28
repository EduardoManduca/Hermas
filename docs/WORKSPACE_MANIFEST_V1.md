# Workspace manifest v1

Status: canonical managed-workspace compatibility boundary.

`manifest.hwm` is exactly 64 bytes. Integers use unsigned little-endian
encoding. Decoders read fields individually and never map the bytes to a
native structure.

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 4 | Magic ASCII `HWM1` |
| 4 | 2 | Manifest version, `1` |
| 6 | 2 | Record size, `64` |
| 8 | 4 | Nonzero workflow ID |
| 12 | 2 | Graph-image version |
| 14 | 2 | Protocol version |
| 16 | 2 | Journal version |
| 18 | 2 | Terminal-result version |
| 20 | 2 | Compensation-token version |
| 22 | 2 | Saga-log version |
| 24 | 8 | Nonzero image fingerprint |
| 32 | 8 | Image size, from 1 byte through the image bound |
| 40 | 24 | Reserved, all zero |

The file must be a regular non-symlink owned by the effective user, have no
group or other permission, and contain exactly one record. The managed
`workflow.hgi` has the same filesystem requirements.

Loading succeeds only when every version equals the current implementation,
the complete managed image passes graph-image validation, and its exact size
and computed fingerprint equal the manifest. There is no fallback, partial
match, version guessing, or state reinterpretation.

The 64-bit image fingerprint binds runtime identities and detects accidental
or hostile changes within the local-user threat model. It is not a package
signature or cryptographic publisher identity.

An incompatible manifest requires a different workspace or a future explicit
migration tool. Editing the version fields does not migrate state.
