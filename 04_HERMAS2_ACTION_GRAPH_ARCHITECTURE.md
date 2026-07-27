# Hermas2 Action-Graph Architecture

## 1. Status of This Document

This document explains the graph semantics of the independent Hermas2
experiment. The [Master Architecture Plan](00_HERMAS2_MASTER_PLAN.md) governs
implementation order and component boundaries.

It retains proven Hermas principles only where they strengthen one central
model:

> An HScript2 workflow is a statically verified, bounded action graph.

HScript2 is the human-readable notation. The action graph is the meaning, and
the graph is directly inspectable by its author.

## 2. Why Start With an Action Graph?

If a language is designed one feature at a time, pipelines, matching, parallelism, deadlines, multiplicity, and sagas can become separate mechanisms with subtly different rules.

An action-graph model gives them a common foundation:

- Nodes describe typed orchestration operations.
- Edges describe typed value flow and execution dependencies.
- Scopes describe bounded lifetime and coordination rules.
- Terminal nodes describe workflow success, known failure, or uncertainty.

The compiler translates readable HScript2 into this graph. The daemon executes
the graph without interpreting source-language syntax.

## 3. Clean-Slate Position

Hermas2 does not preserve the original HScript grammar, workflow image, wire
frames, or C ABI by default. The current Hermas implementation is used to
compare ideas and recover lessons, not to constrain the experiment.

The architecture should nevertheless retain rules that have demonstrated
their value: app-owned processing, nominal typing, bounded execution,
at-most-once delivery, explicit uncertainty, independent validation, C for the
runtime, Rust for compilation, and no C++.

## 4. The Workflow Contract

Every workflow has an explicit public boundary:

```hscript
workflow print_mean(input: GradeRequest)
    -> PrintSuccess
    errors {
        GradeUnavailable,
        CalculationFailed,
        PrintFailed
    }
{
    ...
}
```

The compiler proves that every reachable path ends in:

- The declared success type.
- One of the declared known error types.
- A separate protocol-level `Unknown` terminal wherever complete delivery can
  no longer be resolved.

A workflow may later be referenced as a statically known orchestration component, but it is not a general-purpose function.

## 5. The Semantic Kernel

The language is built from seven concepts.

### 5.1 Typed Values

Values have HSchema2 types. Bindings are immutable.

Local types may be inferred when the originating Action contract makes them unambiguous. Workflow and Action boundaries remain explicit.

### 5.2 Action Invocation

An Action invocation is the only primitive that asks an application to perform work.

An invocation identifies:

- Target app.
- Target Action.
- Input nominal type.
- Success nominal type.
- Declared app-owned failure types.
- Delivery-state transitions.

### 5.3 Explicit Presentation

Presentation intentionally changes the destination nominal type without computing a new payload:

```hscript
grades
    |> as MeanInput
    |> mean_calculator/calculate()
```

The compiler permits this only when recursive representations are compatible. The runtime preserves provenance and validates the source and destination symbol identifiers.

`as` is a presentation operator, not a cast or conversion.

### 5.4 Immutable Binding

`let` names an Action result or a coordination result:

```hscript
let grades = grade_list/get()
    on failure return
    on unknown return
```

There is no reassignment or shared mutable local state.

### 5.5 Exhaustive Choice

Apps may return HSchema2 variants. `match` selects subsequent Actions by case:

```hscript
match decision {
    case approved approval {
        return approval |> destination/accept()
    }

    case rejected reason {
        return reason |> destination/reject()
    }
}
```

Matching provides:

- No predicates.
- No guards.
- No fallthrough.
- No computation.
- Exactly one branch for every declared case.

Initial matching remains terminal. Branch reconvergence can be considered later only if concrete workflows justify its additional join semantics.

### 5.6 Bounded Coordination Scopes

Concurrency and lifetime are introduced only through statically bounded scopes such as:

- `all`
- `within`
- `each` with `collect`
- `saga`

Each scope has explicit capacity, lifetime, delivery, and result rules.

### 5.7 Workflow Completion

Every graph path terminates explicitly:

- Return success.
- Return a declared known failure.
- Return or propagate an uncertain outcome.

There is no implicit fallthrough at the workflow boundary.

## 6. Typed Coordination Contexts

Ease's contexts provide useful inspiration for the compiler model.

Internally, each HScript2 scope can be represented as a typed coordination
context:

```text
Context
├── values available inside the scope
├── maximum concurrent work
├── lifetime rule
├── delivery-state rule
├── completion rule
└── statically known result type
```

The HScript2 surface does not expose a generic `context` facility. Instead, it
uses specific constructs whose behavior is immediately visible.

This avoids turning contexts into shared mutable state or an open-ended concurrency abstraction.

## 7. Structured Concurrency

### 7.1 `all`: Bounded Cooperation

`all` starts a fixed number of independent branches and joins them deterministically:

```hscript
let results = all {
    receipt = order |> receipt_app/create()
    audit   = order |> audit_app/record()
}
on failure return
on unknown return
```

The result is a compiler-generated closed record:

```text
results.receipt
results.audit
```

Rules:

- The branch count is statically bounded.
- Branches share immutable inputs.
- Different apps may progress concurrently.
- Calls to one single-flight app are serialized.
- Results are named; there is no implicit merge.
- Unsent work may be stopped after failure.
- Delivered work is awaited.
- Hermas does not retry or pretend to cancel delivered Actions.
- `Unknown` has precedence over conclusions that require known completion.

### 7.2 `within`: Explicit Lifetime

`within` bounds how long Hermas may deliver and wait:

```hscript
within 5s {
    return request |> payment/authorize()
}
```

Expiration:

- Before delivery: a known not-delivered timeout.
- After delivery: `Unknown`.

The deadline does not cancel application work already delivered. Application-internal timeouts remain app-owned.

### 7.3 `each` and `collect`: Bounded Multiplicity

`each` operates only on a statically bounded HSchema2 list:

```hscript
let reports = each item in orders
    concurrency 4
{
    item |> reporting/create()
}
collect
on failure return
on unknown return
```

Rules:

- The source list has a declared maximum.
- The concurrency ceiling is explicit.
- The ceiling must fit daemon capacity.
- Collected output preserves source order.
- A failure stops admission of unsent elements.
- Already delivered elements are awaited.
- A batch Action is preferred when orchestration across independent apps is unnecessary.

### 7.4 `saga`: Explicit Compensation

A saga is introduced only after durable recovery and compensation inputs are completely specified.

Hermas may order app-declared compensating Actions in reverse dependency order. Apps own the meaning and implementation of compensation.

Every Action contract publishes exactly one compensation capability:

- `compensation NAME` names a distinct app-owned compensating Action. The
  source Action's successful output is the compensation token and must be
  representation-compatible with the compensating Action's input.
- `compensation none` publishes no compensating Action and cannot be used as a
  compensated step in a saga.

This declaration is mandatory contract metadata even when an Action is used
only in an ordinary pipeline. Saga validation rejects an Action with
`compensation none` where compensation could be required, or a named
capability whose relationship or token type is invalid.

Compensation is:

- An ordinary forward Action.
- Explicitly declared.
- Delivered at most once.
- Not an undo primitive.
- Not recursively enrolled for further automatic compensation.

`Unknown` prevents Hermas from guessing which compensations are safe. Reconciliation requires an explicit app contract.

Ordinary pipelines have no implicit compensation.

## 8. The Action Graph

### 8.1 Node Kinds

The initial runtime graph needs a small fixed set of semantic node kinds:

- `Action`
- `Dispatch`
- `Fork`
- `Join`
- `Terminal`

Bindings are compiler names, pipelines are edges, presentations are edge
metadata, and bounded lifetime constructs are regions. They do not become
runtime nodes unless implementation proves that they represent an independent
state transition.

One readable HScript2 expression may deterministically construct several
nodes and edges. The frontend must show that expansion through `graph` and
`explain` commands.

### 8.2 Edge Kinds

Edges are explicit:

- Value edge: carries a typed value.
- Dependency edge: permits execution after a predecessor reaches a required state.
- Failure edge: routes a declared known failure.
- Unknown edge: routes delivery uncertainty.
- Match edge: selects a variant case.
- Compensation edge: records reverse dependency requirements.

No control relationship is inferred dynamically by the daemon.

### 8.3 Static Graph Properties

The compiler proves:

- Every referenced app and Action exists.
- Every value edge has compatible nominal flow.
- Every presentation has compatible representation.
- Every variant match is exhaustive.
- Every branch terminates correctly.
- Every list and fan-out is bounded.
- Every scope fits configured resource limits.
- No forbidden cycle or unbounded recursion exists.
- Every workflow outcome satisfies its public contract.
- Every Action has exactly one valid compensation capability declaration.
- Every named capability references a distinct compensating Action with a
  representation-compatible token input.
- No Action with `compensation none` appears where a saga requires
  compensation.
- Every compensation has a valid input provenance.

The daemon independently validates the encoded structure before loading it.

### 8.4 Effect-Preserving Graph Rules

Actions may have externally visible effects. Graph construction,
normalization, and future optimization must therefore preserve more than final
payload equality:

- An Action invocation is never duplicated or erased.
- Effectful Actions are never reordered merely because no value edge connects
  them.
- Sequence, choice, and parallel regions are not distributed through one
  another unless their complete delivery traces are proven equivalent.
- Parallel regions are not assumed to be associative or commutative. Branch
  identity, resource bounds, failure precedence, and delivery provenance are
  observable.
- Known failure and `Unknown` stop ordinary sequential flow and remain
  distinct outcomes.
- Exhaustive choice selects exactly one branch from the declared variant case.
- Retry, cancellation, and compensation are never inserted implicitly.

Two graph fragments are equivalent only when they preserve nominal contracts,
possible Action-delivery traces, at-most-once behavior, terminal outcomes,
relevant provenance, and declared resource and lifetime bounds. Equal final
payloads alone are not sufficient.

## 9. Delivery Outcomes

Application results and delivery state are different dimensions.

An Action may produce:

```text
NotDelivered(reason)
Succeeded(value)
Failed(app_error)
Unknown(delivery_information)
```

HScript2 may offer concise propagation:

```hscript
let mean = grades
    |> as MeanInput
    |> mean_calculator/calculate()
    on failure return
    on unknown return
```

The syntax is concise, but the graph contains separate failure and unknown edges. The compiler never merges the two categories.

## 10. Graph Inspection

The verified graph is a user-facing artifact. Tooling must show:

- Node and edge identities.
- Action and type names.
- Explicit presentations.
- Error and unknown exits.
- Region membership.
- Fork/join structure.
- Maximum live work and payload storage.
- Source spans.

Visualization is produced from the same verified IR that is encoded for the
daemon. No separate HScript2 interpretation is permitted.

## 11. Graph Image

The Rust compiler emits a read-only, versioned graph image containing:

- Header and graph-image version.
- Workflow contract.
- Workflow-local symbol table.
- Resolved app and Action identifiers.
- Type and representation metadata.
- Node table.
- Typed edge table.
- Match-dispatch tables.
- Parallel-join tables.
- Scope and deadline metadata.
- Resource maxima.
- Compensation topology when applicable.

The graph image is not the execution journal.

- The **graph image** is immutable compiled input.
- The **execution journal** is mutable, append-only runtime history.

They have separate formats and lifecycles.

The image should represent the verified graph rather than reproduce the
HScript2 abstract syntax tree.

## 12. Runtime Execution

`hermas2d` executes a loaded graph using fixed tables and bounded state:

1. Allocate an execution slot from a fixed pool.
2. Attach the workflow input.
3. Mark initially ready nodes.
4. Deliver ready Action nodes when their target app is available.
5. Record delivery transitions.
6. Validate incoming result metadata and payload representation.
7. Activate dependent nodes.
8. Join bounded scopes deterministically.
9. Stop at a terminal outcome.
10. Release the execution arena in constant time.

The daemon does not parse HScript2, resolve names dynamically, calculate
application values, or allocate a new graph.

## 13. Example

```hscript
workflow print_grade_mean(input: Empty)
    -> PrintSuccess
    errors {
        GradeFailure,
        MeanFailure,
        PrintFailure
    }
{
    let grades = grade_list/get()
        on failure return
        on unknown return

    let mean = grades
        |> as MeanInput
        |> mean_calculator/calculate()
        on failure return
        on unknown return

    return mean
        |> as PrintInput
        |> printer/print()
        on failure return
        on unknown return
}
```

Its conceptual graph is:

```text
Input
  |
  v
Invoke grade_list/get
  | success
  v
Present GradeListResult as MeanInput
  |
  v
Invoke mean_calculator/calculate
  | success
  v
Present MeanResult as PrintInput
  |
  v
Invoke printer/print
  | success
  v
Return PrintSuccess

Every Invoke node also has separate known-failure and Unknown edges.
```

The daemon routes the graph. It never stores the grade fixture, calculates the mean, or formats `Mean: 80`.

## 14. Experimental Freedom

Hermas2 may change language spelling, graph encoding, protocol, and ABI when
doing so improves the graph model. It does not need to compile original Hermas
files or run original Hermas applications.

The project should still preserve its own internal continuity: every admitted
feature must compose with the same typed graph kernel and must not create a
parallel special-purpose execution engine.

## 15. Development Order

The architecture should still be implemented through narrow milestones:

1. Freeze core graph terminology and invariants.
2. Implement the Rust graph kernel before parsing HScript2.
3. Specify HSchema2 Action ports.
4. Compile sequential HScript2 expressions into inspectable graphs.
5. Specify the graph image and independent C validator.
6. Execute the Grade Pipeline through the C runtime.
7. Add typed variant dispatch.
8. Add typed `all` joins.
9. Add explicit deadline regions.
10. Add bounded `each` and ordered `collect`.
11. Add higher-level tooling without expanding the language kernel.

Durable restart recovery and saga regions are optional extensions. They enter
the development order only after concrete workflows demonstrate that the
essential local orchestration model is insufficient without them.

Each milestone applies the quality gates for the layer it changes:

- Language work requires written semantics, deterministic diagnostics, and
  compiler tests.
- Runtime work requires static resource accounting, bounded-allocation tests,
  GCC and Clang builds, and sanitizer tests.
- Binary-boundary work requires a versioned layout, independent structural
  validation, golden fixtures, and malformed-input tests.

Early semantic experiments are not required to satisfy gates belonging to
formats or runtime components that do not exist yet.

## 16. Design Standard

The architecture succeeds if HScript2 remains easy to read while every
program has one precise, inspectable graph interpretation.

The intended relationship is:

```text
HSchema2 defines the typed ports exposed by Action nodes.
HScript2 constructs readable graph expressions.
Rust proves and compiles the action graph.
C executes that graph predictably.
Applications perform every meaningful operation.
```
