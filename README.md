# Hermas 2 Design Documents

Hermas2 is an independent experimental reconstruction of Hermas around one
central idea:

> The ideal reconstructed HScript would feel like a small notation for
> constructing a verified action graph—not like a general-purpose language
> with features removed.

Hermas2 has no compatibility obligation to the current Hermas prototype.
The current project remains valuable evidence, a behavioral comparison point,
and a source of proven constraints. It is not a format, grammar, or
implementation constraint on Hermas2.

## Documents

1. [Master Architecture Plan](00_HERMAS2_MASTER_PLAN.md) — authoritative
   architecture, graph model, component boundaries, and implementation
   milestones.
2. [System Architecture](01_HERMAS2_SYSTEM_ARCHITECTURE.md) — concise
   description of the complete system and app/runtime boundary.
3. [End Goal](02_HERMAS2_END_GOAL.md) — intended experience for app
   developers, workflow authors, agents, humans, and ecosystem developers.
4. [Philosophy](03_HERMAS2_PHILOSOPHY.md) — normative principles used to
   admit features and resolve design disagreements.
5. [Action-Graph Architecture](04_HERMAS2_ACTION_GRAPH_ARCHITECTURE.md) —
   detailed explanation of how readable HScript2 expressions construct typed,
   bounded graph fragments.
If two documents disagree, the Master Architecture Plan governs architecture
and the Philosophy governs feature admission. Open questions in either
document remain intentionally unsettled until a milestone resolves them with a
written decision and an executable test.

## Implementation Status

Milestone 1 has a working sequential Rust graph kernel. It currently provides:

- Independent Grade List, Mean Calculator, and Printer Action contracts.
- A directly constructed, verified Grade Pipeline graph.
- Exact nominal edges and explicit representation-compatible presentations.
- Separate success, known app-error, `NotSent`, and delivery-`Unknown` exits.
- Graph bounds, acyclicity, reachability, and port-cardinality verification.
- Deterministic textual explanation.
- Deterministic Graphviz DOT output.
- Static resource reporting.

Milestone 2 has started with an HSchema2 contract frontend. It provides:

- App-owned nominal types and canonical structural representations.
- Unit, integer, Boolean, bounded string/bytes, closed record, bounded list,
  and named typed variant representations.
- Mandatory irreversible or reversible Action classification.
- Typed compensation-contract validation.
- Directional recursive representation compatibility.
- Transactional catalog installation and source-located diagnostics.
- Semantic, layout-independent SHA-256 contract fingerprints.
- Real HSchema2 contracts for all three Grade Pipeline apps.

Milestone 3 has started with a sequential HScript2 frontend. It provides:

- Multi-workflow HScript2 modules whose declarations compile to independently
  addressable verified graphs and separate graph images, using stable
  `module::workflow` identities.
- Workflow input, success, and known-error contracts.
- Action calls and typed pipeline composition.
- Immutable linear bindings with no runtime storage nodes.
- Explicit nominal presentation with recursive representation proof.
- Exact app-error propagation and separate `NotSent`/delivery-`Unknown` exits.
- Deterministic lowering into the existing verified graph.
- Complete node and edge source provenance.
- File-based check, explanation, Graphviz, resource, and source-map commands.

The Grade Pipeline graph now loads the three text contracts and its real
HScript2 source through the same frontend exposed by the CLI.

Milestone 4 has started with graph-image version 1:

- Canonical little-endian header and table offsets.
- Fingerprinted required-app records.
- Closed node and typed-edge records.
- Deterministic Rust encoding.
- A raw-byte structural decoder that independently validates counts, offsets,
  tags, reserved fields, endpoints, port cardinality, terminals, and
  reachability.
- An independent allocation-free C17 decoder built with GCC and Clang.
- A golden Grade Pipeline image digest and malformed/truncation test corpus.

Milestone 5 has started with:

- Explicit 48-byte versioned binary protocol frames.
- Kind-specific identifier, outcome, length, and payload validation.
- Linux `AF_UNIX` `SOCK_SEQPACKET` transport.
- Caller-owned, allocation-free `libhermas2edge` context and buffers.
- Semantic contract fingerprint registration.
- Exactly-once handler dispatch for each completely delivered request.
- A caller-owned, allocation-free sequential graph executor.
- A fixed-capacity required-app registry with exact graph-image fingerprint
  admission.
- A caller-owned 16-slot nonblocking `poll` scheduler with rotating admission,
  fixed packet/value storage, and single-flight app ownership.
- Distinct prepared-before-send `NotSent` and sent-before-loss `Unknown`
  transitions with no automatic retry.
- Canonical input and app-result payload validation against graph-embedded
  representation descriptors.
- A four-process Grade Pipeline integration test that prints `Mean: 80`,
  rejects a mismatched app contract, and proves one delivery per app.
- Socket-level regression tests for pre-send `NotSent`, post-send `Unknown`,
  execution-capacity exhaustion, and same-app serialization.
- Cross-process tests under GCC and Clang with ASan and UBSan.

Milestone 6 now provides exhaustive terminal `match`, named typed variant
Dispatch nodes, exact case-payload routing, and independent malformed-tag and
representation checks in Rust and C.

Milestone 7 has begun with typed bounded `all`: two through eight static
branches lower into Fork/Join records, named result fields retain exact
nominal types, resource inspection reports maximum branch concurrency, and
both independent image decoders validate the new topology.

Its fixed eight-flow C arena now proves real socket overlap across independent
apps, exactly-once branch delivery, same-app serialization, deterministic
failure/`Unknown` precedence, and cutoff of work that was never delivered.

Milestone 8 now includes root and terminal nested `within` regions plus
bounded `each` and ordered `collect`. Positive millisecond/second durations,
parented node ranges, list bounds, item types, template Actions, and explicit
concurrency ceilings are verified in both independent image decoders. The
fixed C runtime expires only flows inside a selected deadline region, admits
item work to the declared ceiling, preserves same-app single-flight, and
collects results in source-index order. Malformed nested parents, zero
durations, invalid bounds, and zero concurrency are covered by executable
negative tests.

The durable-execution-facts extension now provides:

- A canonical 64-byte, little-endian, CRC-checked append-only journal.
- Strict sequence and per-execution transition validation.
- A synchronized `DeliveryPrepared` record before every socket write.
- Synchronized sent, Action outcome, and execution-terminal facts.
- Exclusive Linux writer locking and corruption/truncation refusal.
- Conservative restart classification that closes interrupted prepared or
  sent deliveries as `Unknown` without replaying them.
- Read-only history traversal and the `hermas2_history` inspection tool.
- Crash, lock, malformed-record, failed-write, and daemon-ordering tests.

The explicit-saga foundation has started with a separate durable compensation
token log. It stores opaque reversible-Action success values under exact
execution/request/node identity, retains distinct source and compensation
input Type IDs, synchronizes every append, and rejects corruption, truncated
records, sequence gaps, undersized lookup buffers, and ambiguous duplicate
tokens. Token presence is not treated as forward success; execution-journal
facts remain authoritative.

The compiler and graph-image half of explicit sagas is also present.
Sequential `saga` blocks admit only reversible Actions, are bounded to 16
steps, and lower to dense per-node compensation records. Both the Rust and C
decoders independently verify compensation app admission, token
representations, ordering, uniqueness, and forward Success-edge types.

The bounded saga recovery runtime validates complete execution/token logs,
refuses `Unknown` and inconsistent history, and schedules only
journal-confirmed successes in reverse ordinal order. Compensation invocations
use caller-owned token storage, continue monotonic request IDs, validate typed
results, and stop without retry on `NotSent`, `Unknown`, or app failure.

A separate fixed-record saga attempt log now preserves reverse progress. Its
allocation-free scanner enforces descending ordinals and delivery transitions,
classifies prepared/sent crashes, and prevents a durable compensation success
from being replayed after restart.

## Build and Inspect

From this directory:

```text
cargo test --workspace
cargo run -p herma2 -- check grade-pipeline
cargo run -p herma2 -- explain grade-pipeline
cargo run -p herma2 -- graph grade-pipeline
cargo run -p herma2 -- resources grade-pipeline
cargo run -p herma2 -- schema check apps/grade-pipeline/grade-list.hschema2 apps/grade-pipeline/mean-calculator.hschema2 apps/grade-pipeline/printer.hschema2
cargo run -p herma2 -- workflow check apps/grade-pipeline/grade-pipeline.hscript2 apps/grade-pipeline/grade-list.hschema2 apps/grade-pipeline/mean-calculator.hschema2 apps/grade-pipeline/printer.hschema2
cargo run -p herma2 -- workflow image apps/grade-pipeline/grade-pipeline.hscript2 grade-pipeline.h2gi apps/grade-pipeline/grade-list.hschema2 apps/grade-pipeline/mean-calculator.hschema2 apps/grade-pipeline/printer.hschema2
cargo run -p herma2 -- image check grade-pipeline.h2gi
```

The current Windows GNU linker cannot link build artifacts whose paths contain
spaces. When building from this OneDrive location on Windows, place only
generated Cargo output in a space-free directory:

```powershell
$env:CARGO_TARGET_DIR = 'C:\Temp\hermas2-target'
cargo test --workspace
```

Linux builds do not require that workaround.
