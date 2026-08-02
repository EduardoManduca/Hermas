# Sequential HScript

Status: implemented sequential, typed-choice, bounded-`all`, deadline-region,
and bounded-`each`/ordered-`collect` language slices.

The alpha compiler accepts UTF-8 workflow source files up to 1 MiB each. It
rejects a larger file before parsing so source ingestion remains bounded.

HScript is notation for constructing verified Action graphs. One source file
is a module containing one or more workflow declarations. Each declaration
produces an independent graph with its own contract, resources, deadlines,
terminal outcomes, and execution identity. Sharing a file creates no runtime
relationship between workflows.

The current slice supports workflow contracts, Action invocation, typed pipelines,
immutable linear bindings, explicit nominal presentation, exact known-error
propagation, distinct `NotSent` and delivery-`Unknown` propagation, and
explicit workflow success.

It does not evaluate application values or contain literals, arithmetic,
conditions, retries, loops, mutation, cancellation, or hidden conversion.

## Example

```hscript
workflow grade_pipeline() -> printer::Printed
errors {
    grade-list::GradeError
    mean-calculator::MeanError
    printer::PrintError
}
{
    let grades = grade-list/get()

    let mean = grades
        |> as mean-calculator::MeanInput
        |> mean-calculator/calculate()

    return mean
        |> as printer::PrintInput
        |> printer/print()
}
```

This source creates three Action nodes, four terminal nodes, thirteen edges,
and no runtime representation for either `let`.

## Grammar

```text
module        := module-declaration? workflow+
module-declaration := "module" identifier
workflow      := "workflow" identifier "(" parameter? ")"
                 "->" type-reference
                 "errors" "{" type-reference+ "}"
                 "{" ("within" duration "{")?
                     statement* return-statement
                     "}"? "}"

parameter     := identifier ":" type-reference
type-reference := identifier "::" identifier

statement     := "let" identifier "=" pipeline
               | "let" identifier "=" all-expression
               | "let" identifier "=" each-expression
               | "within" duration "{" return-statement "}"
               | match-expression
return-statement := "return" pipeline
                  | pipeline

pipeline      := invocation continuation*
               | identifier continuation+

continuation  := "|>" invocation
               | "|>" "as" type-reference "|>" invocation

invocation    := identifier "/" identifier "(" ")"
duration      := positive-integer ("ms" | "s")

all-expression := "all" identifier "{"
                    all-branch all-branch+
                  "}"
all-branch    := identifier "=" pipeline

each-expression := "each" identifier "in" identifier
                   "concurrency" positive-integer "{"
                     pipeline
                   "}" "collect" "as" type-reference

match-expression := "match" identifier "{"
                      match-case+
                    "}"
match-case    := "case" identifier identifier "{"
                  "return"? pipeline
                "}"
```

The final pipeline may omit the `return` keyword when it is the only or last
body expression. Commas and semicolons are optional separators. `#` begins a
line comment. `//` is not comment syntax; `/` remains reserved for
`app/action` paths.

Workflow names must be unique within a module. A source containing multiple
workflows must begin with a stable module declaration:

```hscript
module operations

workflow calculate(...) -> ... { ... }
workflow print(...) -> ... { ... }
```

The resulting graph identities are `operations::calculate` and
`operations::print`, and those qualified names are stored in their graph
images. A single legacy workflow may remain unqualified. Module-wide checking
parses, resolves, type-checks, and verifies every declaration. Commands that
inspect or emit one graph require an explicit workflow selection when the
module contains more than one declaration. The current artifact boundary
remains one `.hgi` graph image per selected workflow; the compiler never
merges module members into one graph.

## Workflow boundary

A workflow declares one exact nominal success type and one or more exact
nominal known-error types. Error declaration order is retained in the graph
contract, while duplicate nominal errors are rejected.

A typed parameter becomes the graph's workflow-input port:

```hscript
workflow print(input: printer::PrintInput) -> printer::Printed
errors { printer::PrintError }
{
    return input |> printer/print()
}
```

For a parameterless workflow, the first Action's input type becomes the
workflow-input type. This is intended for Unit or empty-record trigger
contracts but remains an ordinary typed input port in the graph.

The returned Action success must exactly equal the declared workflow success
type. Terminal presentation is not implicit.

## Pipelines and presentation

Without presentation, adjacent ports must use the same nominal catalog type:

```hscript
input |> printer/print()
```

Independently owned types require an explicit presentation:

```hscript
grades
|> as mean-calculator::MeanInput
|> mean-calculator/calculate()
```

The named presentation must be the immediate destination Action's exact input
type. HSchema recursive representation compatibility must also prove that
every source value fits. Presentation becomes edge metadata; it performs no
conversion or payload rewrite.

## Immutable bindings

`let` gives a compiler name to one success output port. It creates no storage
node and cannot be reassigned.

The sequential slice consumes each binding at most once. Fan-out will enter
only through the bounded `all` construct, where branch count, join behavior,
resource use, and failure precedence are explicit. Reusing a sequential
binding is therefore rejected instead of silently creating concurrency.

Every binding must be consumed, every Action success must have exactly one
ordinary successor, and the workflow input is consumed once.

## Typed choice

`match` consumes a named typed variant and lowers to one Dispatch node. Cases
must be exhaustive and unique. Each case binds the exact nominal payload type,
must invoke an Action, and currently terminates at the workflow success type.
Malformed runtime tags are rejected before a branch is entered.

## Bounded `all`

`all` consumes one immutable shared input and creates two through eight fixed
branches:

```hscript
let results = all item {
    left = item |> alpha/run()
    right = item |> beta/run()
}
return results.left |> sink/use()
```

Every branch must begin with the declared shared input and invoke at least one
Action. The compiler lowers the expression to one typed Fork and one typed
Join. Result fields retain their branch's exact nominal type and are accessed
as `results.field`; there is no implicit merge. Unused fields are discarded
only after the Join has observed their completion.

## Deadline regions

`within` currently wraps the complete workflow body:

```hscript
{
    within 5s {
        return input |> payment/authorize()
    }
}
```

The positive duration is compiled into a root deadline region. A terminal
nested region may tighten the budget around the remaining pipeline:

```hscript
{
    let orders = input |> orders/list()
    within 1s {
        return orders |> archive/store()
    }
}
```

The nested record names its parent and exact contiguous node range. Expiry
before complete delivery follows `NotSent`; expiry after complete delivery
follows `Unknown`. Expiry affects only flows currently inside the selected
region and never cancels or retries application work.

## Bounded `each` and ordered `collect`

`each` consumes a named bounded-list binding, admits no more than its explicit
concurrency, and produces one output per source item:

```hscript
let reports = each order in orders concurrency 3 {
    order |> as reporting::Input |> reporting/create()
} collect as reporting::Reports
```

The source and collected types must be named bounded lists. Their element
types must exactly match the item input and Action success respectively, and
the collected bound must cover the source bound. The initial executable slice
permits one Action in the item template. Up to the declared concurrency may be
in flight through that Action's endpoint, with request IDs routing responses
to their item flows. Runtime completion order does not change output order:
`collect` emits items in source-index order. This multiplexing is confined to
items in the same bounded `each` region; ordinary Action delivery remains
single-flight.

## Failure and delivery uncertainty

Each Action has four distinct outgoing paths:

- Success continues through the ordinary typed pipeline.
- The exact app-error type propagates to the known-failure terminal.
- A failed attempt before complete packet delivery propagates to `NotSent`.
- Delivery uncertainty propagates to the `Unknown` terminal.

Implicit app-error propagation is accepted only when the Action's exact
nominal error type occurs in the workflow contract. Compatible
representations or equal short names do not suffice.

`NotSent` and `Unknown` are protocol states, not HSchema types, and require no
workflow error declaration. The compiler always emits them separately.
No path retries an Action.

## Sagas

`saga` marks a sequential pipeline whose successfully completed Actions can be
compensated in reverse order:

```hscript
{
    saga {
        let token = input |> payment/reserve()
        return token |> as shipping::Request |> shipping/book()
    }
}
```

Every forward Action must publish a named compensation in HSchema; an Action
declaring `compensation none` cannot enter a saga. The initial bounded slice
admits 1 through 16 Actions and excludes `all`, `each`, and variant dispatch.
Compilation records each forward node, compensation app and Action, forward
success token type, compensation input/success/error types, and dense forward
ordinal. This metadata is sufficient for strict reverse runtime scheduling
without loading the compiler catalog.

## Source provenance

Every compiler-emitted node and edge retains a file, byte offset, byte length,
line, and column. Terminals map to their workflow contract declarations;
Actions map to invocations; value edges map to their invocation or pipeline
operator; failure, `NotSent`, and `Unknown` edges map to the Action invocation.

The source map is part of the verified in-memory graph and is emitted by the
same frontend that produces explanation and Graphviz output.

## Inspection

Until catalog packaging is implemented, the CLI receives the workflow followed
by its local schemas:

```text
hermas workflow check MODULE SCHEMA...
hermas workflow explain [--workflow NAME] MODULE SCHEMA...
hermas workflow plan [--workflow NAME] MODULE SCHEMA...
hermas workflow plan --json [--workflow NAME] MODULE SCHEMA...
hermas workflow graph [--workflow NAME] MODULE SCHEMA...
hermas workflow resources [--workflow NAME] MODULE SCHEMA...
hermas workflow sources [--workflow NAME] MODULE SCHEMA...
hermas workflow image [--workflow NAME] MODULE OUTPUT.hgi SCHEMA...
```

Selection is optional only for a single-workflow module. `check` always
validates and reports every workflow.

`plan` reports dependency-derived readiness stages, bounded `all`/`each`
parallelism, deadline scopes, and saga recovery order. It is deliberately not
a predicted runtime trace. HScript statement order assigns names and
constructs edges; it does not independently serialize Actions. Actions in the
same readiness stage have no data dependency between them, but actual overlap
still depends on branch selection, app availability, per-Action ownership,
capacity, and the runtime scheduler. A bounded `each` item Action is the
exception to ordinary single-flight ownership and may carry up to the
region's explicit concurrency.

`plan --json` emits the versioned `hermas-workflow-plan-v1` automation
projection documented in `WORKFLOW_PLAN_JSON_V1.md`. It exposes verified
Action node, numeric protocol identity, readiness stage, resource, and bounded
parallel-region metadata without asking integrations to infer IDs from source
order. It is compiler diagnostic metadata, not a workflow value; HSchema
remains the contract language for all bytes delivered to Actions.

The Grade Pipeline proof source is
`apps/grade-pipeline/grade-pipeline.hscript`. Its integration path parses the
three HSchema files, compiles this HScript source, verifies the resulting
graph, and then exposes that same graph to every inspection command. It has
no built-in compiler or CLI path; it is processed as an ordinary external
workflow.
