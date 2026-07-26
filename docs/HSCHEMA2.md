# HSchema2 Contract Language

Status: implemented language contract for Milestone 2.

HSchema2 describes one application's identity, nominal boundary types, Action
ports, and effect classification. It contains no workflow, transport,
deployment, handler, or business-logic configuration.

## Contract shape

```hschema2
app warehouse

type Empty = record {}
type ItemId = String<32>
type Quantity = Integer
type Request = record {
    id: ItemId
    quantity: Quantity
}
type Requests = List<Request, 16>
type Failure = variant {
    unavailable: Request
    invalid: Empty
}
type Receipt = Bytes<64>

action release {
    input Receipt
    success Empty
    error Failure
    kind irreversible
}

action reserve {
    input Request
    success Receipt
    error Failure
    kind reversible compensate release
}
```

Commas and semicolons are optional separators. `#` and `//` begin line
comments. Declarations and record or variant members may be reordered without
changing the contract fingerprint.

## Representations

Every `type` declaration creates a nominal type owned by the file's app. Its
right-hand side defines the canonical wire representation.

| Form | Meaning |
| --- | --- |
| `Unit` | No payload bytes |
| `Integer` | Signed 64-bit integer representation |
| `Boolean` | One-byte Boolean representation |
| `String<N>` | Length-prefixed UTF-8 bytes bounded by `N` |
| `Bytes<N>` | Length-prefixed uninterpreted bytes bounded by `N` |
| `record { name: Type ... }` | Closed, named-field product |
| `List<Type, N>` | Length-prefixed homogeneous list bounded by `N` |
| `variant { case: Type ... }` | Named, typed, closed choice |

All byte and list bounds are mandatory. The current implementation limit is
`1..=1,048,576` bytes for `String` and `Bytes`, `1..=256` list elements, 256
types per app, 256 Actions per app, and 64 levels of representation nesting.
Recursive representations are rejected.

Record field and variant case order is not semantic. Names and recursive
representations are semantic. An empty record is valid; an empty variant is
not.

## Nominal identity and presentation

Equal representations do not make two nominal types equal. A direct graph
edge requires the same catalog type ID. HScript2 will require an explicit
presentation when independently owned nominal types differ.

Presentation is permitted only when every source value fits the destination:

- Primitive kinds match exactly.
- A source string or byte bound does not exceed the destination bound.
- Closed records have the same field names and recursively compatible fields.
- A source list bound does not exceed the destination bound and its element is
  recursively compatible.
- Variants have the same case names and recursively compatible payloads.

Presentation never converts, relabels fields, truncates, allocates, or invokes
an Action.

## Action contracts

Each Action declares exactly one input, success, and app-error nominal type.
All three types must be declared by the same app.

The `kind` clause is mandatory:

- `kind irreversible` declares an Action for which Hermas2 has no automatic
  compensation relationship.
- `kind reversible compensate NAME` declares that the Action's successful
  value is a compensation token accepted by the named Action.

The compensation must be a distinct Action in the same contract. The
reversible Action's success representation must fit the compensation Action's
input representation. Compensation remains an ordinary forward Action;
Hermas2 does not infer an undo operation.

Delivery uncertainty is not an HSchema2 app-error type. Every invocation
acquires a separate delivery-`Unknown` graph port from the execution model.

## Contract fingerprints

`herma2` canonicalizes nominal declarations while preserving named type
references, orders nominal types, Actions, record fields, and variant cases by
name, and serializes the result with the versioned `hermas2-contract-v1`
canonical form. The contract fingerprint is the SHA-256 digest of that
canonical form.

Whitespace, comments, separators, and declaration order therefore do not
change identity. Nominal names, representations, Action ports, effect kinds,
and compensation relationships do.

## Checking contracts

```text
herma2 schema check app-one.hschema2 app-two.hschema2
```

Files are installed transactionally into one local compilation catalog. A
failed file leaves that catalog unchanged. Diagnostics have stable
`stage/code` labels and byte, line, and column locations:

```text
contract.hschema2:7:18: schema/unknown-type: unknown type `Missing`
```

The three executable proof contracts live in
`apps/grade-pipeline/*.hschema2`. The directly constructed Grade Pipeline
loads these files through the same parser and catalog path used by the CLI.
