# Hermas2 Graph Image Version 1

Status: implemented Rust encoder and structural decoder plus an independent,
allocation-free C17 decoder. Both accept the Rust-produced Grade Pipeline
golden image and reject their malformed structural suites.

All integers are unsigned little-endian values read field-by-field. Records
are byte layouts, never native Rust or C structures. Offsets are absolute from
the beginning of the image.

## Canonical layout

```text
header (80 bytes)
workflow error Type IDs (u16[])
zero padding to four-byte alignment
required app records (36 bytes each)
type records (8 bytes each)
node records (8 bytes each)
edge records (16 bytes each)
region records (16 bytes each)
recursive representation descriptors
UTF-8 workflow name
```

There are no trailing bytes. Every table offset must equal the canonical
offset calculated from preceding counts. Version 1 contains no source map;
source provenance remains compiler-side data under the current open decision.

## Header

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `H2GI` |
| 4 | 2 | Version, `1` |
| 6 | 2 | Header size, `80` |
| 8 | 4 | Exact total image size |
| 12 | 4 | Flags, zero |
| 16 | 4 | Workflow-name offset |
| 20 | 2 | Workflow-name byte length |
| 22 | 2 | Workflow input Type ID |
| 24 | 2 | Workflow success Type ID |
| 26 | 2 | Error count |
| 28 | 2 | Required-app count |
| 30 | 2 | Node count |
| 32 | 2 | Edge count |
| 34 | 2 | Type count |
| 36 | 4 | Error-table offset |
| 40 | 4 | App-table offset |
| 44 | 4 | Type-table offset |
| 48 | 4 | Node-table offset |
| 52 | 4 | Edge-table offset |
| 56 | 4 | Representation-table offset |
| 60 | 4 | String-table/name offset |
| 64 | 4 | Representation-table byte length |
| 68 | 2 | Region count, currently `0..=16` |
| 70 | 2 | Reserved, zero |
| 72 | 4 | Region-table offset |
| 76 | 4 | Reserved, zero |

IDs are nonzero. Workflow errors are unique. Counts must fit the graph
kernel's configured limits.

## Required apps

Each app record contains:

```text
u16 app_id
u16 reserved_zero
u8  semantic_contract_sha256[32]
```

Only apps referenced by Action nodes occur, ordered by App ID. Encoding fails
if an app was not installed from a fingerprinted HSchema2 contract.

## Types and representations

Each type record stores a nonzero graph-local Type ID, a zero reserved field,
and the absolute offset of its recursive canonical representation descriptor.
Descriptors are contiguous, canonical, and bounded before the string table.
They let the runtime validate payload bytes without the compiler catalog.

## Nodes

Each node record contains:

```text
u8  kind             // 1 Action, 2 Terminal, 3 Dispatch, 4 Fork, 5 Join
u8  subtype          // terminal outcome or Fork/Join branch count
u16 action_id        // Action ID; Dispatch/Fork nominal Type ID
u16 app_id           // Action only
u16 reserved_zero
```

Exactly one terminal of every subtype must occur. Action app IDs must occur in
the required-app table.

## Edges

Each edge record contains:

```text
u8  source_kind      // 0 Input, 1 Success, 2 AppError, 3 NotSent, 4 Unknown,
                     // 5 DispatchCase, 6 ForkBranch, 7 JoinField,
                     // 8 EachItem, 9 EachOutput
u8  target_kind      // 1 ActionInput, 2 Terminal, 3 Dispatch, 4 Fork, 5 Join,
                     // 6 EachInput, 7 EachCollect
u8  flags            // bit 0: explicit presentation; all others zero
u8  port_tag         // Dispatch case, Fork branch, Join input/field; otherwise zero
u16 source_node      // zero only for WorkflowInput
u16 target_node
u16 source_type      // zero only for NotSent and Unknown
u16 target_type      // zero only for NotSent and Unknown
u16 presentation_type
u16 reserved_zero
```

When presentation is set, `presentation_type` equals `target_type`.
Otherwise it is zero. Known-failure terminal types must occur in the workflow
error table; success terminal types equal the workflow success type; NotSent
and Unknown edges carry no nominal type.

Dispatch case descriptors must structurally equal their edge source nominal
representation. Fork branch tags are dense and carry the Fork input type.
Join input tags are dense; a used Join field has exactly the same nominal type
as its corresponding input.

## Regions

Deadline records are ordered parent-before-child:

```text
u8  kind             // 1 Deadline
u8  flags            // zero
u16 first_node
u16 node_count
u16 parent_region    // zero for root, otherwise one-based Deadline ID
u64 duration_ms      // nonzero
```

The duration is a relative execution budget. Clock selection and expiry
notification belong to the daemon; the image contains no wall-clock instant.

An `each` record describes a bounded dynamic expansion without creating
unbounded graph nodes:

```text
u8  kind             // 2 Each
u8  concurrency      // 1..=min(bound, 8)
u16 template_node    // exactly one Action in the current slice
u16 source_list_type
u16 item_input_type
u16 item_output_type
u16 collected_list_type
u16 bound
u16 reserved_zero
```

Both decoders prove the source list's element representation and exact bound,
the collected list's output representation and sufficient bound, the
template's Action success type, and all four Each edge cardinalities.

A saga step is one explicit compensation mapping:

```text
u8  kind                 // 3 SagaStep
u8  flags                // zero
u16 forward_node         // Action node
u16 compensation_app
u16 compensation_action
u16 source_token_type    // forward Action success
u16 destination_type     // compensation Action input
u16 ordinal              // dense, one-based forward order
u16 reserved_zero
```

Every step is followed immediately by its compensation outcome contract:

```text
u8  kind                 // 4 SagaOutcome
u8  flags                // zero
u16 forward_node
u16 compensation_success_type
u16 compensation_error_type
u64 reserved_zero
```

Saga pairs follow Deadline and Each records. If any occur, every Action node
has exactly one pair, ordinals are dense, the compensation app is required by
the image, and the two token representations are structurally equal. The
forward node's Success edge must carry `source_token_type`. Outcome Type IDs
let the runtime validate both successful and app-error compensation results.

## Structural validation

The decoder rejects:

- Truncation and trailing bytes.
- Unknown magic or version.
- Nonzero flags or reserved fields.
- Noncanonical, overlapping, misaligned, or overflowing offsets.
- Zero, duplicate, or excessive counts and IDs.
- Invalid UTF-8 or an empty workflow name.
- Duplicate app records, errors, nodes, or edges.
- Unknown record tags and invalid endpoint kinds.
- Missing or multiply connected Action input/success/error/NotSent/Unknown
  ports.
- Missing workflow input flow.
- Missing or duplicate terminals.
- Unreachable nodes and closed cycles.

Rust decoding is intentionally performed from raw bytes independently of the
encoder's in-memory records. The C implementation must apply the same checks
without casting the image to native structures.

The C decoder in `src/image.c` uses fixed arrays bounded by the image limits,
performs no allocation, and reads every integer explicitly. Its parity test
validates the Rust-produced golden image, every truncated prefix, and focused
header/offset mutations under both GCC and Clang with warnings as errors.
