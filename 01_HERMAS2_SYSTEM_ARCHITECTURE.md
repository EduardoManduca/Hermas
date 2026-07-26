# Hermas2 System Architecture

Status: independent clean-slate experiment. The
[Master Architecture Plan](00_HERMAS2_MASTER_PLAN.md) is authoritative.

## 1. What Hermas2 Is

Hermas2 is a local action protocol and orchestration system for coordinating
independent applications on a single Linux host. It explores a graph-first
architecture without a compatibility obligation to the original Hermas
prototype.

Applications expose typed semantic operations called **Actions**. A Hermas2
user composes those Actions into an inspectable, verified action graph without
controlling application interfaces, reproducing business logic inside the
orchestrator, or coupling the participating applications directly to one
another.

Hermas2 occupies the boundary between applications:

```text
Workflow requests an Action
            |
            v
Hermas validates and routes the request
            |
            v
Application performs the Action
            |
            v
Hermas validates and routes the result
```

Hermas2 coordinates graph execution. Applications perform computation and own
the meaning of their operations.

The governing rule is:

> Let the app process the actions; keep Hermas simple.

## 2. The App/Hermas2 Boundary

Hermas2 is responsible for:

- Discovering registered applications and their Action contracts.
- Treating the verified action graph as the public semantic meaning of every
  workflow.
- Compiling readable HScript2 expressions into verified action graphs.
- Explaining and visualizing those graphs before execution.
- Checking nominal types and representation compatibility.
- Assigning stable workflow-local symbol identifiers.
- Routing bounded binary frames between applications.
- Tracking delivery and execution state.
- Enforcing workflow dependencies, deadlines, and bounded concurrency.
- Reporting known failures and uncertain outcomes honestly.

Applications are responsible for:

- Business logic and computation.
- Domain validation.
- External I/O such as databases, networks, files, devices, and user interfaces.
- Authorization decisions and capability verification.
- Retry policy internal to the application.
- The meaning and implementation of compensation.
- Returning a result that conforms to the declared Action contract.

Hermas must never become a hidden application framework. It does not calculate means, filter records, format reports, execute SQL, call HTTP APIs, or interpret domain values. Those behaviors belong in Actions.

Every HSchema2 Action contract declares exactly one effect kind:
`reversible` or `irreversible`. A reversible Action must name a distinct
app-owned compensating Action, and its successful output must be
representation-compatible with that compensating Action's input because the
successful output is the compensation token. An irreversible Action has no
compensation declaration. This classification is always present in contracts,
although automatic compensation is available only inside an explicit saga
scope after durable recovery and compensation storage have been specified.

## 3. The Main Components

### 3.1 HSchema2

**HSchema2** is the contract and data-definition language.

It defines:

- Nominal types.
- Primitive representations.
- Closed records.
- Bounded lists.
- Typed variants.
- Action inputs, outputs, and declared application errors.
- Representation compatibility between independently named types.

Nominal typing prevents accidental interchange:

```hschema
type UserId from String
type PostalCode from String
```

`UserId` and `PostalCode` may use the same physical representation, but they are not the same semantic type.

HSchema2 is recursively explicit, closed by default, and does not perform
silent coercion.

### 3.2 HScript2

**HScript2** is the graph-construction language.

It expresses:

- Sequential Action pipelines.
- Immutable typed bindings.
- Explicit nominal presentation using `as`.
- Exhaustive selection over app-owned variants.
- Bounded parallel coordination.
- Explicit deadlines.
- Bounded multiplicity.
- Explicit saga scopes when durable compensation is available.

HScript2 is a DSL by choice, not a restricted general-purpose language. Its
constructs exist because they describe graph topology, typed flow, lifetime,
delivery, or dependencies. It contains no general computation, arbitrary
loops, classes, operating-system threads, dynamic dispatch, or unbounded task
creation.

One HScript2 file may serve as a module containing multiple workflow
declarations. Each declaration is compiled and executed as an independent
verified graph. File membership does not imply shared execution state,
dependencies, deadlines, failure propagation, or terminal outcomes.
Multi-workflow modules declare a stable module name, and their graph images
carry qualified `module::workflow` identities rather than path-derived names.

### 3.3 `herma2`: Rust Frontend

`herma2` is the provisional name for the developer-facing compiler and
command-line interface, written in Rust.

Its responsibilities include:

- Parsing HSchema2 and HScript2.
- Resolving application and Action names against the local catalog.
- Performing nominal and representation type checking.
- Proving exhaustiveness, bounds, dependency validity, and resource limits.
- Constructing one explicit graph IR per workflow declaration and calculating
  each graph's maximum resources independently.
- Emitting deterministic graph explanations and visualizations.
- Compiling workflows offline and deterministically.
- Emitting a verified, read-only graph image.
- Providing inspection, validation, and explicit test tooling.

Rust is used where complex parsing, diagnostics, and static analysis benefit from strong memory safety.

### 3.4 `hermas2d`: C Runtime Daemon

`hermas2d` is the provisional name for the execution daemon, written in C.

It:

- Loads previously compiled graph images.
- Accepts app registrations.
- Routes Action requests and results through Unix Domain Sockets.
- Advances ready nodes in already verified action graphs.
- Validates runtime frame metadata and payload structure.
- Tracks bounded concurrent workflows in a nonblocking event loop.
- Maintains explicit delivery states.
- Uses configured pools or arenas so workflow execution cannot exceed declared
  resource limits.

The daemon does not reinterpret the workflow or perform application work. Its job is predictable execution of a bounded plan.

### 3.5 `libhermas2edge`

`libhermas2edge` is the provisional name for the small C-ABI library used at
the application boundary.

It helps an application:

- Connect to `hermas2d`.
- Register its identity and contract fingerprint.
- Receive checked Action frames.
- Dispatch requests to application-owned handlers.
- Return typed successes or failures.

Language bindings may wrap this ABI for Rust, Zig, Go, Python, Node.js, or other languages. Apps remain separate processes. Hermas does not load plugins into the daemon.

### 3.6 Native Apps and Connectors

A **Native Hermas2 App** directly exposes Actions through the Hermas2
protocol.

A **Connector** is a Native Hermas2 App whose Actions bridge to ordinary
software such as:

- PostgreSQL.
- HTTP services.
- SMTP servers.
- Filesystems.
- Message brokers.
- AI services.
- Existing command-line programs.

Connectors keep foreign protocols and policies outside the Hermas core.

## 4. Communication Model

The initial Linux runtime is planned to use Unix Domain Sockets (`AF_UNIX`)
with `SOCK_SEQPACKET`. Hermas2 may redesign the exact frame layout; it retains
the rule that the wire format is explicitly serialized and independent from
the C ABI.

The wire format is explicitly specified:

- Fixed-width little-endian fields.
- Protocol version and frame kind.
- Execution and request identifiers.
- Application and Action symbol identifiers.
- Source and destination nominal type identifiers.
- Outcome and payload length.
- Reserved fields that must be zero.

Socket bytes are never blindly cast to C structures. Every field, length, bound, and type identifier is validated before use.

Nominal annotations are carried in frame metadata. Binary payloads contain canonical representations. An explicit `as` presentation can preserve the payload bytes while changing the destination nominal metadata, but only when the compiler and daemon establish representation compatibility.

## 5. Delivery Semantics

Hermas2 uses at-most-once delivery. It never silently retries an Action.

An invocation can reach one of these operational states:

- `NotSent`: delivery did not occur.
- `Sent`: the complete request was delivered.
- `Succeeded`: a valid success result arrived.
- `Failed`: a valid, known failure arrived.
- `Unknown`: delivery occurred, but Hermas cannot determine the final application outcome.

For example, a timeout before delivery is known not to have executed the Action. A disconnect after complete delivery may be `Unknown`. Hermas does not hide that uncertainty behind a generic error or an automatic retry.

## 6. Memory and Resource Model

Runtime structures are bounded:

- Fixed connection limits.
- Fixed active-workflow limits.
- Statically bounded lists and fan-out.
- Configured pools or arenas with explicit capacities.
- No unbounded allocation on the execution path.

Bounds are part of correctness, not merely performance tuning. The compiler rejects workflows whose maximum resource requirements cannot fit the target runtime.

## 7. Compilation and Execution Lifecycle

```text
1. Apps publish HSchema2 contracts
2. Local catalog records available contracts
3. User writes an HScript2 graph expression
4. `herma2` resolves it and constructs a verified graph
5. The user may inspect the graph and its resource summary
6. `herma2` emits a read-only graph image
7. `hermas2d` independently validates and loads the image
8. Apps connect outward and register
9. `hermas2d` executes the bounded action graph
10. Apps perform Actions and return typed results
11. Hermas2 records and reports the execution outcome
```

Compilation is offline and deterministic. Production execution never depends on reparsing source files or consulting a remote registry.

## 8. Architecture Status

Hermas2 is an architectural experiment and does not yet have a production
implementation. It will be built in narrow, verified milestones:

- Establish the graph kernel before final syntax.
- Establish HSchema2 Action ports.
- Compile sequential HScript2 expressions into inspectable graphs.
- Establish the graph image, binary protocol, and edge API.
- Execute a real multi-application Grade Pipeline.
- Add structured graph expressions only after their semantics are coherent.

The original Hermas remains a comparison point, not an implementation base or
compatibility constraint. Hermas2 succeeds only if the graph-first model makes
the implementation more coherent in practice.
