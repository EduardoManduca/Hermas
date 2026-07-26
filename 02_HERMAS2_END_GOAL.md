# Hermas2 End Goal

Status: product vision for the independent Hermas2 experiment.

## 1. Vision

The long-term vision is:

> Hermas2 is a local, decentralized action protocol and orchestration system
> that lets applications expose typed semantic operations, allowing agents,
> scripts, and humans to construct and inspect verified action graphs without
> micromanaging graphical interfaces.

Hermas should make application capabilities available as safe, discoverable building blocks.

A user should be able to express **what applications should do together**, while each application remains responsible for **how its own work is done**.

## 2. The Problem Hermas Solves

Applications commonly expose their capabilities through unrelated interfaces:

- Graphical user interfaces.
- Command-line options.
- HTTP APIs.
- Language-specific libraries.
- Plugin systems.
- Database schemas.
- File formats.

Automation often reproduces fragile interface behavior: clicking buttons, managing windows, locating files, scraping text, or manually adapting incompatible APIs.

Hermas2 introduces a shared semantic boundary. Instead of saying:

> Open the grade application, export a file, run a calculator, copy the result, open a printer application, and paste the text.

A user can request:

```text
grade_list/get
    -> mean_calculator/calculate
    -> printer/print
```

The applications are still independent. They only agree to expose typed Actions.

## 3. The Developer Experience

An application developer should be able to:

1. Describe the app's semantic types in HSchema2.
2. Declare Actions with explicit input, success, and failure contracts.
3. Implement handlers in the developer's preferred language.
4. Register the running app with the local Hermas daemon.
5. Test the contract using deterministic development tooling.
6. Allow workflows to use the app without anticipating every future composition.

The developer does not need to know:

- Which workflows users will write.
- Which other apps will participate.
- Whether the caller is a person, script, or agent.
- How Hermas2 users organize source or graph-image files.
- Which user interface initiated the Action.

The app publishes capabilities, not predefined integrations.

## 4. The HScript2 User Experience

A Hermas2 user should primarily need HScript2 and local discovery tools.

The workflow author should be able to:

- Search installed apps and Actions.
- Inspect Action descriptions and contracts.
- Compose compatible Actions as readable graph expressions.
- Inspect the exact nodes, edges, scopes, outcomes, and resource limits created
  by those expressions.
- Explicitly present compatible nominal values.
- Handle declared failures exhaustively.
- Express bounded parallel work.
- Establish explicit deadlines.
- Inspect execution results and uncertainty.

The user should not need to:

- Manage socket files.
- Assign protocol symbol identifiers.
- Construct binary frames.
- Edit app manifests manually.
- Launch or supervise production apps through HScript2.
- Write glue code for ordinary compatible pipelines.
- Understand the implementation language of an app.

## 5. The Agent Experience

Agents are important Hermas users, but Hermas is not designed only for AI.

An agent should be able to inspect a finite local catalog containing:

- App identity.
- Action name and description.
- Typed input.
- Typed success result.
- Declared failure variants.
- Resource and delivery properties.

This gives an agent a more reliable control surface than graphical automation or loosely described tool calls.

Static compilation provides a safety boundary before execution:

- Unknown Actions are rejected.
- Type mismatches are rejected.
- Missing error cases are rejected.
- Unbounded fan-out is rejected.
- Invalid presentations are rejected.
- Workflows exceeding daemon capacity are rejected.

The agent can propose behavior freely within a small, mechanically checked language. Hermas does not need to trust the agent to construct raw protocol messages correctly.

## 6. The Human Experience

Humans may interact with Hermas through:

- HScript2 source.
- A friendly terminal interface.
- A graphical action-graph editor.
- An application that generates HScript2.
- An agent that explains and requests confirmation for a workflow.

These interfaces should all construct the same verified action-graph
representation. The textual DSL remains the clearest normative notation, but
Hermas2 does not embed user-interface behavior in the daemon.

A graphical editor is therefore a client of Hermas, not part of Hermas's execution semantics.

## 7. The Application Ecosystem

Hermas2 remains useful by allowing external software to add capabilities as
Actions.

Possible ecosystem apps include:

- Data conversion and validation services.
- HTTP, database, email, and filesystem connectors.
- Queues and schedulers.
- Retry-policy services.
- Authorization and approval services.
- AI inference and agent services.
- Reporting and observability tools.
- Domain-specific compensation and reconciliation services.
- Package discovery and installation tools.

These are ordinary isolated applications communicating through the same public protocol. The daemon does not load privileged in-process extensions.

This creates an important division:

> Hermas2 provides the rules for constructing verified action graphs. The
> ecosystem provides the Actions worth placing in those graphs.

## 8. Decentralization

Hermas2 is decentralized at the application level:

- Each app owns its contracts.
- Each app owns its data.
- Each app owns its business rules.
- Each app can be implemented independently.
- Apps do not need direct dependencies on one another.
- Workflows create temporary coordination without permanently coupling the apps.

`hermas2d` is a local coordinator, not a central owner of application behavior
or domain truth.

The initial system targets one Linux host. Distributed networking can be provided later by explicit gateway or connector apps rather than silently changing the local protocol's guarantees.

## 9. What Success Looks Like

Hermas2 reaches its intended end state when:

- Applications can expose semantic operations with little integration code.
- Users compose useful behavior primarily through HScript2.
- Users can see and understand the complete verified graph they created.
- Agents discover and invoke Actions through precise contracts.
- Independent apps can participate without knowing one another.
- Incorrect compositions fail before execution.
- Runtime delivery uncertainty is represented honestly.
- Concurrency remains structured and bounded.
- Adding new capabilities normally means adding an app, not expanding HScript2.
- The C graph executor remains small enough to understand, test, audit, and
  optimize as one state machine.
- Protocol behavior is consistent across every supported application language.

## 10. What Hermas2 Is Not Trying to Become

Hermas2 is not:

- A general-purpose programming language.
- A replacement for application business logic.
- A distributed database.
- A universal message broker.
- A container orchestrator.
- A graphical automation engine.
- An in-process plugin runtime.
- An implicit retry framework.
- A system that guarantees exactly-once external side effects.
- A way to hide uncertainty after a delivered request.

Those boundaries are essential to achieving the end goal. Hermas becomes more useful by remaining dependable at its chosen layer, not by absorbing every adjacent capability.
