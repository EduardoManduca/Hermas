# Hermas2 Philosophy

Status: normative feature-admission and design philosophy.

## 1. Governing Principle

> Let the app process the actions; keep Hermas simple.

The language-design principle is:

> The ideal reconstructed HScript would feel like a small notation for
> constructing a verified action graph—not like a general-purpose language
> with features removed.

Hermas2 exists to let users construct and execute typed action graphs between
applications. It does not exist to replace those applications or become a
home for their business logic.

Every design decision should reinforce that boundary.

## 2. The Feature-Admission Rule

The normative rule for new features is:

> If behavior can be implemented as one typed Action, it does not belong in
> HScript2.
> If behavior describes typed graph flow, delivery state, lifetime, or
> dependencies across multiple Actions, it may belong in Hermas2.

This rule does not automatically admit the second category. A proposed feature must still demonstrate:

- A concrete orchestration requirement.
- Semantics that compose with the graph kernel without a separate execution
  mechanism.
- Static bounds or a compelling bounded representation.
- Deterministic failure and delivery behavior.
- A specification shared by the compiler and runtime.
- A benefit that cannot be obtained cleanly from an ordinary app Action.

## 3. Coordination, Not Computation

HScript2 constructs relationships between Actions:

- What value reaches which Action.
- Which nominal presentation is intentional.
- Which Actions depend on earlier results.
- Which independent Actions may overlap.
- Which result selects the next Action.
- How long Hermas may wait.
- Which compensations are ordered after known failures.

HScript2 does not calculate values. Arithmetic, filtering, transformation,
policy, formatting, parsing, and domain decisions are application behaviors.

This makes HScript2 small while allowing the Hermas2 ecosystem to become broad.

## 4. Explicitness Over Guessing

Hermas does not guess across application boundaries.

It favors:

- Nominal types over accidental structural interchange.
- Explicit presentation over implicit conversion.
- Closed records over silently ignored fields.
- Exhaustive matching over fallthrough.
- Declared errors over exceptions escaping a contract.
- Fixed bounds over dynamically growing runtime state.
- Versioned binary layouts over native-structure assumptions.
- Explicit uncertainty over a fabricated success or failure.

Convenient syntax is welcome when it preserves these guarantees. Convenience must not make important behavior invisible.

## 5. The Graph Must Be Visible

Hermas2 users should be able to inspect the exact verified graph produced by
their source:

- Action nodes.
- Typed edges and presentations.
- Branch and join structure.
- Scope lifetimes.
- Every terminal outcome.
- Maximum resource requirements.

Readable syntax may lower to multiple graph records, but it may not hide
Actions, conversion, retry, cancellation, or unbounded work. The compiler must
be able to explain every runtime record in terms of a source span.

## 6. Consistency Before Feature Count

Hermas should have few concepts with strong, reusable rules.

The grammar, compiler, graph image, daemon, edge library, documentation,
diagnostics, and fixtures must describe the same system. A feature is
incomplete if those layers disagree.

New abstractions should not be added merely because they are common in other languages or impressive in a feature list. Every abstraction creates obligations:

- Syntax.
- Static semantics.
- Binary representation.
- Runtime state.
- Failure behavior.
- Resource accounting.
- Diagnostics.
- Compatibility rules.
- Tests.

A smaller coherent system is more powerful than a large contradictory one.

## 7. Boundedness Is a Semantic Guarantee

Hermas runs workflows using bounded memory and bounded runtime structures.

Therefore:

- Lists have declared maximum lengths.
- Parallel branches have fixed limits.
- Fan-out has an explicit concurrency ceiling.
- Workflow resource requirements are computable before execution.
- The daemon does not allocate arbitrary runtime graphs.
- Unbounded recursion and spawning are excluded.

This is not solely an optimization. It gives the compiler, daemon, operator, and user a shared understanding of the maximum execution shape.

Boundedness does not prohibit dynamic allocation. A runtime may use configured
pools or arenas when their capacities are explicit and exhaustion has a
defined outcome. The guarantee is the absence of unbounded growth, not loyalty
to one allocation technique.

## 8. At-Most-Once Means No Guessing

Hermas delivers an Action at most once. It does not silently retry after a timeout or disconnect.

After complete delivery, the application may have performed an irreversible side effect even if its response is lost. Hermas reports that state as `Unknown`.

This principle requires discipline:

- `Unknown` is not converted into a known failure.
- A deadline does not cancel an already delivered Action.
- Compensation is not attempted when Hermas cannot safely determine what succeeded, unless an explicit reconciliation contract exists.
- Retry policies belong to visible application Actions.

Honest uncertainty is safer than convenient fiction.

## 9. Application Sovereignty

Apps own:

- The semantic meaning of their types.
- Domain validation.
- Authorization.
- Computation.
- Side effects.
- Internal concurrency.
- Internal retries.
- Compensation implementation.

Hermas owns:

- Contract-level representation.
- Cross-Action type flow.
- Delivery state.
- Dependencies.
- Bounded orchestration lifetime.
- Protocol correctness.

This separation allows apps to evolve and use different implementation languages without surrendering their domain responsibilities to the coordinator.

## 10. A Small Core and a Large Ecosystem

Hermas does not need a language feature for every desired behavior.

External apps can provide Actions for:

- Conversion.
- Filtering.
- Batching.
- Queuing.
- Scheduling.
- Retrying.
- Networking.
- Database access.
- AI processing.
- Policy decisions.
- Reporting.

Those capabilities remain composable through Hermas2 without increasing the
complexity or privilege of `hermas2d`.

Extensions should use the same process isolation and public protocol as every other app. Hermas does not create a special in-process plugin class.

## 11. Language and Implementation Philosophy

Hermas uses programming languages according to component needs:

- **C** for the bounded runtime, wire handling, daemon, and edge ABI.
- **Rust** for parsing, compilation, static analysis, developer tooling, and
  safe production of graph images.
- **No C++** in the project.

This is not language branding. It is a concrete architectural division:

- The C runtime should remain small, predictable, and easy to bind.
- The Rust frontend may perform sophisticated analysis without placing that complexity in the daemon.
- Apps may use any language capable of speaking the protocol or calling the C ABI.

## 12. Inspiration Without Imitation

Hermas can learn from Unix, Linux, Git, CSP, structured concurrency, functional languages, and Ease. It should not imitate them to appear prestigious.

Useful inspirations include:

- Unix: small components connected through explicit interfaces.
- Linux and Git: consistency, performance awareness, and resistance to unnecessary abstraction.
- CSP and structured concurrency: bounded lifetimes and understandable concurrent relationships.
- Functional languages: immutable values and exhaustive typed choice.
- Ease: coordination through typed, scoped contexts rather than shared local mutation.

Hermas adopts an idea only when it naturally strengthens Hermas's own guarantees.

## 13. Freedom Without Aimlessness

Hermas2 has no obligation to preserve the original Hermas grammar, image,
wire protocol, ABI, or special-case workflow layouts. Both systems are
prototypes, and the experiment should be free to discover a better model.

Incompatibility is not itself progress. A divergence should make the graph
clearer, the static proof stronger, the runtime smaller, or the app boundary
simpler. Existing Hermas behavior is evidence to study, not a rule to obey.

## 14. Questions for Every New Design

Before adding a feature, ask:

1. Does this coordinate multiple Actions, or perform application work?
2. Could an ordinary typed Action provide it more cleanly?
3. What values cross the boundary?
4. Are those values copied, presented, or consumed?
5. Who owns the operation's lifetime?
6. What happens before and after delivery?
7. How is `Unknown` represented?
8. Is the maximum resource use statically bounded?
9. Can the compiler and daemon validate it independently?
10. Does it compose through the graph kernel without a special executor?

If the answers are unclear, the feature is not ready.

## 15. The Standard to Preserve

Hermas should be understandable as one consistent system:

```text
Apps declare semantic Actions.
HSchema2 declares their typed ports.
HScript2 constructs readable graph expressions.
The compiler makes the graph visible and proves it.
The daemon delivers it at most once.
Apps perform the work.
Hermas reports exactly what it knows.
```

That is the project's philosophy in operational form.
