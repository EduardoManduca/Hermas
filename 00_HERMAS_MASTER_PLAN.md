# Hermas Master Architecture Plan

## 1. Project Charter

Hermas is an experimental, clean-slate action protocol and orchestration
system for independent applications on one Linux host.

Applications publish typed semantic operations called **Actions**. HScript
users construct verified action graphs from those operations. The Hermas
runtime routes requests, tracks delivery, and advances the graph. Applications
perform all business logic and domain work.

The two governing statements are:

> Let the app process the actions; keep Hermas simple.

> The ideal HScript would feel like a small notation for
> constructing a verified action graph—not like a general-purpose language
> with features removed.

This repository is the definitive Hermas implementation. The earlier
prototype remains useful historical evidence, but it is not a source, image,
ABI, wire, grammar, or state compatibility target.

## 2. Why Hermas Exists

The legacy prototype demonstrated that the basic philosophy
works:

- Apps can own processing while Hermas coordinates them.
- Nominally typed Action boundaries prevent accidental interchange.
- A small C runtime can route real multi-process workflows predictably.
- At-most-once delivery and explicit `Unknown` outcomes are practical.
- Bounded concurrency can work without user-visible threads or unbounded
  runtime allocation.

It also revealed that the executed object is naturally a graph: Action results
enable other Actions, variants select branches, parallel scopes fork and join,
and error results follow separate edges.

Hermas explores what happens when that graph is the language's public semantic
model from the beginning instead of primarily a compact runtime encoding.

## 3. Compatibility Position

Hermas may reuse good ideas, but it does not preserve compatibility merely
because an idea has already been implemented.

Hermas may change:

- Original HSchema grammar.
- Original HScript grammar.
- File extensions.
- Workflow boundary syntax.
- Error-handling syntax.
- Presentation syntax.
- Graph-image layout.
- Wire frames.
- C ABI.
- Daemon state representation.
- Package and catalog layout.

Every divergence must improve at least one of:

- Semantic clarity.
- Graph transparency.
- Static safety.
- Runtime auditability.
- Resource predictability.
- Implementation consistency.
- App-developer simplicity.

The legacy prototype remains a valuable comparison oracle.
Equivalent scenarios should be compared when useful, but equivalence is an
experiment—not an obligation.

## 4. Core Mental Model

Hermas has five primary objects:

1. **HSchema contracts** define app-owned nominal values and Action ports.
2. **HScript expressions** construct typed graph fragments.
3. **The verified action graph** is the complete semantic meaning of a
   workflow.
4. **The graph image** is a compact, read-only encoding of that verified
   graph.
5. **An execution** is bounded mutable state traversing one loaded graph.

The relationship is:

```text
Apps declare possible Action nodes through HSchema
                         |
                         v
HScript composes typed graph fragments
                         |
                         v
Rust constructs and verifies an action graph
                         |
                         v
Rust emits a compact graph image
                         |
                         v
C validates and executes the graph
                         |
                         v
Apps receive Actions and perform the work
```

The graph is not hidden compiler machinery. Developers can inspect it,
explain it, visualize it, and calculate its maximum resource requirements
before execution.

## 5. Architectural Boundaries

### 5.1 Hermas owns

- Contract-level type and representation checking.
- Action discovery and static name resolution.
- Construction and verification of action graphs.
- Dependency, flow, and scope semantics.
- Bounded runtime scheduling.
- Protocol framing and structural payload validation.
- At-most-once delivery state.
- Known orchestration failure and delivery uncertainty.
- Execution history needed to explain what Hermas did.

### 5.2 Applications own

- Business logic.
- Arithmetic and transformation.
- Domain validation.
- Authorization decisions.
- External protocols and storage.
- Internal retries.
- Internal concurrency.
- User interfaces.
- The meaning of compensation and reconciliation.

### 5.3 HScript does not contain

- Arithmetic or string processing.
- User-defined computational functions.
- Mutable variables.
- General loops or recursion.
- Classes or inheritance.
- Threads, futures, or task handles.
- Dynamic Action lookup during execution.
- Unbounded fan-out.
- Implicit conversion.
- Silent retry.
- In-process plugins.

These are not removed general-language features. They are outside the object
HScript describes.

## 6. Language and Runtime Components

Names are provisional during the experiment.

### 6.1 HSchema

HSchema defines:

- App identity.
- Nominal types.
- Canonical representations.
- Closed records.
- Statically bounded lists.
- Named typed variants.
- Action input, success, and app-error ports.
- A mandatory public compensation capability: `none` or a named Action.
- For every named compensation capability, a declared Action whose input
  representation accepts the source Action's success output.

An Action is conceptually:

```text
InputType -> Action -> SuccessType | AppErrorType
```

Delivery uncertainty is not an app-owned HSchema error case. It belongs to
the Hermas execution protocol.

The capability declaration is part of the Action contract even before saga
execution is implemented. `compensation none` publishes no automatic
compensation relationship. A named capability references a different
app-owned Action, and the source Action's successful output is the compensation
token containing every value the app needs to compensate the effect.
Compensation remains an ordinary forward Action, not an undo primitive, and
invoking it as compensation does not recursively enroll it for automatic
compensation.

### 6.2 HScript

HScript is a graph-construction DSL.

An HScript source file is a module and may declare multiple workflows.
Every workflow declaration constructs one independent verified action graph
with its own public input, terminal outcomes, deadlines, resource bounds, and
execution identity. Merely sharing a source file creates no graph edge,
dependency, lifetime, failure propagation, or runtime coordination between
workflows.

Every multi-workflow source declares an explicit stable module name. Its
workflow graphs use qualified identities of the form `module::workflow`;
filenames and directory locations are not workflow identity. A legacy
single-workflow source may remain unqualified. Module-local names remain
convenient selectors only after the containing module is already known.

Unrelated behavior should be expressed as separate workflow declarations,
even when those declarations are maintained in one file. `all` and `each`
remain the constructs for concurrent work whose branches jointly belong to
one workflow outcome. The frontend must make every workflow in a module
individually addressable for checking, inspection, compilation, deployment,
and execution.

Its core surface concepts are:

- Action invocation.
- Typed pipeline composition.
- Immutable naming of graph outputs.
- Explicit nominal presentation.
- Exhaustive variant matching.
- Bounded parallel composition.
- Explicit deadline regions.
- Bounded replication and ordered collection.
- Explicit compensation regions after recovery is specified.
- Explicit workflow terminals.

Every expression has a statically known:

- Input port.
- Success output port.
- Set of app-error outputs.
- Delivery-uncertainty behavior.
- Maximum graph expansion.
- Maximum runtime resource requirement.

### 6.3 `hermas`: Rust Frontend

The Rust frontend owns:

- HSchema and HScript parsing.
- HScript module parsing and unique workflow-name resolution.
- Source diagnostics.
- Local catalog resolution.
- Type and representation analysis.
- Graph construction.
- Graph normalization.
- Graph verification.
- Static resource calculation.
- Graph explanation and visualization.
- Deterministic graph-image emission.

The frontend may allocate freely while compiling. The production C execution
path uses configured pools or arenas and must not grow without a declared
bound. Allocation strategy is an implementation choice; bounded resource use
is the architectural requirement.

### 6.4 `hermasd`: C Runtime

The C daemon owns:

- Graph-image decoding and independent validation.
- App registration and contract identity checks.
- A bounded nonblocking event loop.
- Fixed execution, connection, flow, and packet slots.
- Delivery-state transitions.
- Graph-node readiness and completion.
- Structural payload validation.
- Append-only execution history.

It does not parse HScript, resolve source names, build graphs, supervise apps,
or execute business logic.

### 6.5 `libhermas_edge`

The edge library provides a small caller-owned C ABI for:

- Connecting an app to the daemon.
- Registering app and contract identity.
- Receiving checked Action requests.
- Dispatching to app-owned handlers.
- Returning canonical typed success or app-error payloads.

Other languages use thin bindings or implement the versioned wire protocol.
There is one protocol definition; bindings may not invent alternative
semantics.

## 7. C-Like Semantic Transparency

Hermas should describe its execution model as directly as C describes a
machine-oriented computation model.

This does not mean copying C syntax. It means avoiding invisible orchestration
work.

| HScript source | Graph/runtime meaning |
| --- | --- |
| Action call | One Action node that may be delivered once |
| Pipeline | A typed success dependency edge |
| Presentation | Edge metadata; no conversion or payload rewrite |
| `match` | Exhaustive typed dispatch |
| `all` | Fixed fork, bounded branches, deterministic join |
| `within` | One explicit lifetime region |
| `each` | Bounded replication of one graph fragment |
| `collect` | Ordered bounded join |
| `saga` | Explicit forward compensation dependencies |
| Binding | Compiler name for an output port; no runtime storage abstraction |

The language must not silently:

- Insert an Action.
- Convert a payload.
- Retry delivery.
- Cancel delivered work.
- Merge values.
- Create an unbounded task.
- Change application policy.

Readable expressions may lower to multiple graph records, but lowering is
deterministic, documented, and inspectable.

## 8. Graph Model

### 8.1 Graph fragments

An HScript expression constructs a graph fragment. A fragment exposes:

```text
input ports
success output
app-error outputs
delivery-unknown exit
resource summary
```

Composition connects ports. It does not call a hidden evaluator during
compilation or runtime.

### 8.2 Runtime node kinds

The initial semantic graph uses a closed, minimal set:

- **Action** — requests one app-owned operation.
- **Dispatch** — selects one variant case.
- **Fork** — makes fixed independent successors ready.
- **Join** — waits under one declared deterministic policy.
- **Terminal** — completes with success, known failure, timeout/not-sent, or
  `Unknown`.

Not every source-language construct becomes a node:

- `let` is compiler-only naming.
- Presentation is typed edge metadata.
- Pipelines are edges.
- Source blocks become verified graph regions.
- Type names become resolved graph-local identifiers.

New runtime node kinds require a concrete execution behavior that cannot be
represented by existing nodes, edges, or regions.

### 8.3 Edge kinds

Edges are closed and typed:

- Success-value edge.
- App-error edge.
- Variant-case edge.
- Dependency-only edge.
- Delivery-unknown edge.
- Compensation edge.

An edge records its source and destination port types. An explicit
presentation records both nominal identities and the proof requirement for
their canonical representations.

### 8.4 Regions

Regions attach lifetime and resource semantics to bounded subgraphs:

- Deadline region.
- Bounded-replication region.
- Compensation region.

Regions are used when separate enter/exit nodes would add encoding complexity
without adding an observable state transition.

### 8.5 Graph restrictions

The initial graph is:

- Closed before execution.
- Deterministic in topology.
- Forward-only.
- Statically bounded.
- Free of runtime graph mutation.
- Free of arbitrary cycles.
- Free of dynamically selected Action identities.

`each` uses a statically bounded graph template rather than an unrestricted
cycle.

## 9. Workflow Outcomes

App errors and protocol outcomes remain separate.

An Action attempt can produce:

```text
Succeeded(Value)
Failed(AppError)
NotSent(Reason)
Unknown(DeliveryInformation)
```

`Unknown` means complete delivery may have occurred but Hermas cannot know
the app's final side-effect state.

Initial HScript rules:

- Success may follow a typed success edge.
- App failure may follow an explicit typed error edge or propagate to a
  workflow error terminal.
- Not-sent work produces a known orchestration outcome.
- `Unknown` terminates automatic graph progression.
- Hermas never retries automatically.
- Hermas never runs compensation after `Unknown` without an explicit,
  separately completed reconciliation Action.

This keeps uncertainty outside app-owned domain variants.

## 10. HScript Expression Direction

Exact grammar remains a milestone decision. The intended reading model is:

```hscript
workflow grade_pipeline() -> printer::Printed
    error printer::PrintError
{
    grade_list/get()
    |> as mean_calculator::MeanInput
    |> mean_calculator/calculate()
    |> as printer::PrintInput
    |> printer/print()
}
```

The expression constructs three Action nodes and typed edges. Default error
propagation is permitted only when the compiler can resolve an unambiguous
declared workflow error. No delivery retry or conversion is implied.

A bounded parallel expression may produce a typed closed product:

```hscript
let results = all grades {
    mean = grades
        |> as mean_calculator::MeanInput
        |> mean_calculator/calculate()

    audit = grades
        |> as audit::GradeInput
        |> audit/record()
}

results.mean
|> as printer::PrintInput
|> printer/print()
```

`all` is an expression because it has an input, a deterministic typed result,
and explicit bounded lifetime. It is not a statement that escapes ordinary
composition rules.

## 11. Graph Inspection

Graph visibility is a product feature, not optional debugging output.

The frontend should eventually provide:

```text
hermas check module.hscript
hermas graph module.hscript WORKFLOW
hermas explain module.hscript WORKFLOW
hermas resources module.hscript WORKFLOW
hermas compile module.hscript WORKFLOW
```

Module-wide checking validates every declaration. Commands that inspect or
emit one graph select its workflow explicitly; no command may silently merge
unrelated workflow declarations into one graph.

Inspection should show:

- Node IDs and Action identities.
- Typed ports and presentations.
- Success, error, case, and unknown edges.
- Fork and join membership.
- Region boundaries.
- Maximum concurrent deliveries.
- Maximum live payload bytes.
- Required applications.
- All terminal outcomes.
- Source spans for every graph record.

The graph may be emitted as deterministic text and a standard visualization
format such as Graphviz DOT. Visualization is derived from the verified IR,
not separately interpreted from HScript.

## 12. Graph Image

The graph image is a compact, versioned, read-only binary artifact.

It should contain only runtime-relevant data:

- Header and format version.
- Graph and workflow descriptors.
- Contract fingerprints.
- Graph-local symbols or resolved numeric identities.
- Node table.
- Typed edge table.
- Region table.
- Resource maxima.
- Canonical dispatch and join tables where needed.
- Source-map data only if runtime explanation justifies its cost.

It should not contain:

- Parser AST nodes.
- Local binding names unless retained for diagnostics.
- Comments.
- Redundant names required only at compile time.
- Mutable execution state.
- Journal records.

The C decoder reads every integer explicitly. The image is never cast to native
structures.

## 13. Runtime Execution Model

The runtime uses fixed storage:

```text
Daemon
├── connection slots
├── registered app slots
├── loaded graph descriptors
├── execution slots
│   ├── node state
│   ├── ready set
│   ├── fixed flow slots
│   ├── region state
│   └── outcome state
└── fixed packet buffers
```

For each execution:

1. Validate the workflow input.
2. Initialize the precomputed root-ready set.
3. Deliver ready Action nodes when their target apps are available.
4. Mark complete delivery before awaiting an app result.
5. Validate result metadata and canonical payload representation.
6. Activate successor nodes according to the verified edge table.
7. Resolve bounded joins deterministically.
8. Finish at one explicit terminal.
9. Release the execution arena in constant time.

The runtime must not discover graph topology by scanning arbitrary memory or
constructing dynamic dependency objects.

## 14. Concurrency Model

Hermas concurrency is graph concurrency:

- Independent ready nodes may progress concurrently.
- One Action connection is single-flight. Different Actions of the same app
  may use independent endpoints and progress concurrently.
- `all` creates a fixed fork and join.
- `each` admits at most its declared concurrency ceiling.
- Two independent workflows may progress simultaneously.
- Already delivered Actions are never treated as canceled.
- Failure stops admission of work whose dependency has not become ready.
- Join outcome ordering is deterministic and documented.

No user-visible threads, futures, races, or task handles are required.

## 15. Journaling and Recovery

The graph image and execution history are distinct:

- The graph image says what may execute.
- The execution journal says what was observed during one execution.

Durability is added only after the in-memory state machine is stable.

The journal records delivery facts rather than app domain state. If durable
sagas are added, compensation tokens use a separately specified durable
representation. Hermas does not turn the delivery journal into an
application-data store.

## 16. Ecosystem Model

Third-party capabilities are ordinary isolated apps exposing HSchema
contracts.

Examples include:

- HTTP and database connectors.
- Conversion and validation services.
- Queues and schedulers.
- Explicit retry-policy services.
- Authorization and approval services.
- AI and agent services.
- Reporting and history tools.
- Reconciliation and compensation services.

There is no in-process daemon plugin model. An extension must tolerate the same
protocol, typing, delivery, and isolation rules as every other app.

## 17. Repository and Implementation Shape

The initial repository should remain small:

```text
Hermas/
├── README.md
├── docs/
├── compiler/
│   └── hermas/          # Rust
├── include/
│   └── hermas/         # Public C ABI
├── src/                 # C runtime and protocol
├── apps/
│   └── grade-pipeline/  # Independent proof apps
├── tests/
└── CMakeLists.txt
```

Rust uses Cargo inside the compiler directory. C17 and CMake/Ninja build the
runtime. C++ is prohibited.

No directory is added until a milestone has code or documentation that belongs
there.

## 18. Implementation Milestones

### Milestone 0: Architecture freeze for the first slice

Deliver:

- This master plan.
- A terminology glossary.
- Written graph invariants.
- A decision record for workflow outcomes.
- A decision record for nominal presentation.
- A small set of example graphs expressed as deterministic text.

Gate:

- Every runtime concept in the first slice maps to a graph record or a clearly
  compiler-only concept.

### Milestone 1: Rust graph kernel

Implement without HScript parsing:

- Closed node, edge, port, region, and terminal types.
- Caller-independent graph builder.
- Graph normalization.
- Complete verifier.
- Resource summary.
- Deterministic textual graph dump.

Construct test graphs directly in Rust.

Gate:

- Reject dangling ports, type mismatch, unreachable nodes, invalid terminals,
  cycles, excess bounds, invalid forks/joins, and unhandled app errors.

### Milestone 2: HSchema

Specify and implement:

- Primitive representations needed by the Grade Pipeline.
- Nominal declarations.
- Closed records.
- Bounded lists.
- Named typed variants.
- Action declarations.
- Canonical representation compatibility.
- Contract fingerprints.

Gate:

- HSchema can fully describe the three Grade Pipeline apps without runtime
  application code.

### Milestone 3: Sequential HScript

Implement:

- Workflow contracts.
- Action calls.
- Pipelines.
- Immutable bindings.
- Presentation.
- Explicit or provably unambiguous app-error propagation.
- Graph inspection commands.

Gate:

- The compiler emits exactly one deterministic graph for the Grade Pipeline,
  and every graph element maps back to a source span.

### Milestone 4: Graph image and C decoder

Specify and implement:

- Version 1 graph-image layout.
- Rust encoder.
- Independent C decoder and verifier.
- Golden byte fixtures.
- Malformed-image suite.

Gate:

- Rust and C accept the same complete valid corpus and reject the same
  malformed structural cases.

### Milestone 5: Protocol, edge ABI, and sequential runtime

Implement:

- Explicit binary frames.
- `AF_UNIX` `SOCK_SEQPACKET` transport.
- Caller-owned `libhermas_edge`.
- Bounded C event loop.
- App registration.
- Sequential graph execution.
- At-most-once and `Unknown`.
- Grade List, Mean Calculator, and Printer apps.

Gate:

- The independent three-process pipeline prints `Mean: 80`.
- Invocation counts prove at-most-once delivery.
- Runtime allocation remains within configured, testable limits.

### Milestone 6: Typed choice

Implement:

- HSchema variants.
- HScript `match` expressions.
- Dispatch nodes.
- Optional typed branch reconvergence when all output ports agree.

Gate:

- Exhaustive, duplicate, missing, malformed-tag, incompatible-join, and source
  diagnostic tests pass.

### Milestone 7: Bounded parallel expressions

Implement:

- Typed `all`.
- Fork and join records.
- Closed product results.
- Deterministic failure and `Unknown` precedence.
- Same-app serialization.

Gate:

- Real overlap occurs across independent apps, never beyond declared capacity,
  and every delivered branch is invoked at most once.

### Milestone 8: Regions and multiplicity

Implement separately:

1. Root and then nested `within` regions.
2. Bounded `each` graph templates.
3. Ordered `collect`.

Gate:

- Timeout-before-delivery differs from post-delivery `Unknown`.
- Replication respects source bounds, concurrency limits, cutoff, order, and
  fixed memory.

### Optional extension A: Durable execution facts

This extension is admitted only when real workflows require execution facts
to survive daemon restart. If admitted, implement:

- Append-only journal.
- Prepared-before-send durability rule.
- Startup classification of interrupted executions.
- History inspection.

Gate:

- Crash tests never replay a prepared or sent Action.

### Optional extension B: Explicit saga regions

This extension is not part of the essential Hermas implementation. It is
admitted only after durable recovery exists and multiple concrete workflows
demonstrate that application-level reconciliation is insufficient. If
admitted, specify:

- App-declared compensation Actions.
- Durable compensation tokens.
- Reverse dependency scheduling.
- Compensation failure and reconciliation boundaries.

Gate:

- No automatic compensation occurs after `Unknown` without an explicit
  reconciliation result.

## 19. Quality Gates

Every milestone requires only the gates relevant to the layer it changes.

All milestones require:

- Written semantics for changed behavior.
- Deterministic tests and diagnostics where user input is involved.
- Explicit, testable resource bounds where runtime resources are involved.
- Focused regression tests for the invariants affected by the change.

Serialized-format and trust-boundary milestones additionally require:

- A versioned format specification.
- Independent validation of safety-critical encoded structure.
- Golden fixtures.
- Malformed-input tests for every encoded length, offset, tag, ID, and
  reserved field.

C runtime and protocol milestones additionally require:

- GCC and Clang builds with warnings as errors.
- AddressSanitizer and UndefinedBehaviorSanitizer.
- No unchecked interpretation of socket bytes as native structures.
- At-most-once invocation tests.
- Allocation accounting that proves configured bounds are respected.

Performance is measured after correctness. A graph abstraction is rejected if
it prevents clear bounds or requires unbounded dynamic runtime graph
construction.

## 20. Architecture Evaluation

Hermas is successful only if the graph-first model proves itself in code.

Compare it with the legacy prototype on:

- Number of independent runtime control mechanisms.
- Number of state transitions.
- Ease of explaining complete execution.
- Reuse of validation rules across features.
- Amount of feature-specific daemon code.
- Maximum-memory calculation.
- Malformed-input surface.
- Compiler diagnostic quality.
- App-developer burden.
- HScript readability.
- Throughput and latency.

Hermas is allowed to be incompatible. It is not allowed to be sophisticated
without becoming more coherent.

## 21. Open Decisions

These are intentionally deferred:

- Final HSchema and HScript file extensions.
- Exact workflow error-propagation syntax.
- Whether presentation retains the word `as`.
- Whether graph-image source maps ship in production images.
- Whether typed `match` joins enter the first choice milestone.
- Whether Action connections may advertise capacity above one.
- Exact journal persistence strategy.
- Package format and remote registry.

Each decision should be resolved at the latest milestone that requires it, not
prematurely.

## 22. Final Architectural Test

A contributor should be able to explain Hermas in one sequence:

```text
HSchema defines typed Action ports.
HScript connects those ports into readable expressions.
The Rust frontend constructs and proves a bounded action graph.
The graph image carries only verified runtime facts.
The C daemon advances ready nodes and records delivery honestly.
Applications perform all computation and side effects.
```

If a feature cannot fit that explanation without exceptions, it probably does
not belong in Hermas.
