# HSchema2 Contract Language

Status: implemented language contract for Milestone 2.

The standard source form is exactly one HSchema2 file per application. That
file describes the application's identity, shared nominal boundary types,
Action ports, and public compensation capabilities. It contains no workflow,
transport, deployment, handler, business-logic configuration, or claim that a
compensation restores an application's internal state.

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
    compensation none
}

action reserve {
    input Request
    success Receipt
    error Failure
    compensation release
}
```

Commas and semicolons are optional separators. `#` begins a line comment.
`//` is not comment syntax. Declarations and record or variant members may be
reordered without changing semantic fingerprints.

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

The `compensation` clause is mandatory:

- `compensation none` declares that the contract publishes no automatic
  compensation relationship for this Action.
- `compensation NAME` publishes the named Action as a compensation capability.
  The first Action's successful value is the token accepted by `NAME`.

The named compensation must be a distinct Action in the same contract. The
source Action's success representation must fit the compensation Action's
input representation. Compensation remains an ordinary forward Action.
HSchema2 does not claim literal reversibility, prove business-level recovery,
or infer an undo operation. The app developer owns those semantics; HScript2
decides whether a workflow enrolls the capability in an explicit saga.

Delivery uncertainty is not an HSchema2 app-error type. Every invocation
acquires a separate delivery-`Unknown` graph port from the execution model.

## Schema and Action fingerprints

`herma2` emits two related identities:

- The schema fingerprint covers the complete app file through the versioned
  `hermas2-contract-v2` canonical form. It is useful for catalog inspection.
- Each Action fingerprint covers one Action's name, ports, compensation
  declaration, and only the transitive closure of nominal types used by those
  declarations through `hermas2-action-v1`. Graph images and daemon
  registration use this narrower identity.

Both are SHA-256 digests of canonical descriptive bytes. SHA-256 is used as a
stable content identifier, not for encryption, authentication, secrecy, or
trust. Hermas2 has no cryptographic key dependency.

Whitespace, comments, separators, and declaration order do not change either
identity. Adding an unrelated type or Action changes the schema fingerprint
but leaves existing Action fingerprints unchanged. Changing a referenced
nominal name or representation, an Action port, or its compensation capability
changes that Action's fingerprint.

An app may declare many Actions in its one file. At deployment, each Action
registers as its own endpoint. Numeric Action IDs remain catalog-local: the
daemon matches `(app identity, Action fingerprint)` and translates between the
graph image's Action ID and the app's current local Action ID.

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
`apps/grade-pipeline/*.hschema2`. They are ordinary external inputs loaded
through the same parser and catalog path as every third-party contract.
