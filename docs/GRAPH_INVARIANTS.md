# Hermas Graph Kernel Invariants

Status: normative through the bounded-parallel graph slice.

The first kernel represents sequential Action graphs directly in Rust. It does
not encode a graph image yet. HSchema and sequential HScript now construct
this same kernel rather than defining parallel execution models.

## Source provenance

1. Compiler-emitted graphs attach one source location to every node and edge.
2. A source location records file, byte offset, byte length, one-based line,
   and one-based column.
3. Direct Rust construction may omit provenance for focused verifier tests.
4. Provenance is explanatory compiler data and does not alter graph validity,
   topology, nominal typing, or runtime behavior.

## Catalog

1. App, type, and Action IDs are nonzero and stable within one catalog.
2. App names are unique.
3. Type names are unique within their app.
4. Action names are unique within their app.
5. Every initial Action input, success, and app-error type is owned by the
   Action's app.
6. A list bound is `1..=256`.
7. Explicit presentation requires recursive representation compatibility.
8. A source list bound must not exceed the destination list bound.

## Graph

1. The graph is closed before verification.
2. The initial kernel admits at most 64 nodes and 192 edges.
3. Node and edge identities are deterministic insertion-order IDs.
4. The closed node set contains Action, Dispatch, Fork, Join, and exactly one terminal for each
   workflow outcome: success, known app failure, `NotSent`, and delivery
   `Unknown`.
5. The workflow has one input nominal type, one success nominal type, and a
   closed set of known app-error nominal types.
6. Every Action has exactly one incoming input edge.
7. Every Action has exactly one success, app-error, not-sent, and
   delivery-unknown exit.
8. A success edge may enter another Action or the success terminal.
9. An app-error edge may enter a recovery Action or the known-failure terminal.
10. A not-sent edge enters only the `NotSent` terminal; a delivery-unknown edge
    enters only the `Unknown` terminal. Both carry no nominal payload or
    presentation.
11. An edge without presentation requires exact nominal identity.
12. An edge with presentation first proves representation compatibility and
    then requires exact destination nominal identity.
13. All nodes are reachable from the workflow input.
14. The graph is acyclic.
15. No Action delivery, conversion, retry, cancellation, or value merge is
    implicit in the graph.
16. A Dispatch has one typed variant input and exactly one typed output per
    canonical case tag.
17. A Fork has one typed input and between two and eight fixed, identically
    typed branch outputs.
18. A Join has one typed input per Fork branch. Each named result field keeps
    its exact nominal branch type and has at most one consumer.
19. Fork/Join topology is closed before execution; no branch identity or count
    is created at runtime.
20. The initial `within` slice admits at most one nonzero-duration root
    deadline region covering every node. It changes delivery lifetime, not
    topology, app cancellation, or retry behavior.

## Grade Pipeline

The first accepted graph is:

```text
grade-list/get
    -> as mean-calculator::MeanInput
    -> mean-calculator/calculate
    -> as printer::PrintInput
    -> printer/print
```

Each app owns independently named types. The two `as` edges prove compatible
representations without changing payload bytes. Every Action error reaches the
known-failure terminal. A failed pre-delivery attempt reaches `NotSent`; every
uncertain post-delivery failure reaches the separate `Unknown` terminal.
