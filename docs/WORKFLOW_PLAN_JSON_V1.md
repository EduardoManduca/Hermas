# Workflow plan JSON v1

Status: implemented automation contract.

```text
hermas workflow plan --json [--workflow NAME] MODULE SCHEMA...
```

The command validates the same HSchema catalog and HScript graph as ordinary
image compilation, then writes one UTF-8 JSON object followed by one newline.
It performs no workflow execution and creates no runtime or durable state.

The exact top-level format identifier is `hermas-workflow-plan-v1`. The object
contains:

- `workflow`: the selected qualified workflow name.
- `semantics`: dependency-driven scheduling metadata. A false
  `source_order_guarantee` states that textual statement order is not a hidden
  execution-order promise.
- `resources`: the verified bounded graph counts, maximum ready Action
  concurrency, and maximum canonical payload size.
- `actions`: Action nodes in graph-node order. Each entry gives `node_id`,
  `app_id`, `action_id`, the qualified `name`, and its dependency-derived
  `stage`.
- `parallel_regions`: bounded `all` and `each` regions. An `all` entry gives
  its Fork node and fixed branch count. An `each` entry gives its template
  node, static item bound, and concurrency ceiling.

Consumers must require the exact `format` before interpreting the document.
They may ignore additional object members. Changing the meaning or JSON type
of an existing member requires a new format identifier.

This projection exists for diagnostics, history attribution, build tooling,
and agent automation. It is not accepted by the daemon, cannot be sent as an
Action value, and is not an alternative contract or workflow language.
HSchema and HScript remain authoritative; graph images remain the daemon's
validated executable input.
