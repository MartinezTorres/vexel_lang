# Optional search advice

The finite solver accepts a local executable through `--heuristic PROGRAM`.
It starts the program with no arguments, sends one JSON request on standard
input, and reads one JSON response from standard output. No shell interprets
the program name or its response. The executable is explicitly selected and
trusted to run on the host; it can be an offline heuristic or an adapter to a
model service. Vexel itself requires neither credentials nor a network call.

For example:

```sh
make
build/vexel examples/constraints.vx --heuristic ./examples/branch_provider.py \
    --search-report build/search.json
build/vexel examples/constraints.vx --heuristic-replay build/search.json
```

A puzzle settled by propagation needs no advice. Use a problem needing numeric
or structural decisions to observe provider calls. For example, a choice used
as an array dimension requests a structural alternative before the complete
relation graph exists. The test suite includes successful, impossible and
ambiguous problems whose resolution requires search.

## Protocol

Requests use `format: "vexel-branch-1"`. They contain:

- `problem`: a content fingerprint of the variable domains and relation tables;
- `id`: a fingerprint of this request's current branch state;
- `depth`: the search depth;
- `kind`: `"numeric"` or `"structural"`;
- `sequence`: the request number within the owning puzzle, across revisions;
- `fallback`: the variable ordinary search would choose;
- `variables`: currently unfixed variables, each with an integer `id`, a source
  `name`, a `choice` Boolean and its active `domain` of decimal integer strings.

A structural request exposes one variable: the finite decision already requested
by the resolver. Its `context` includes the solve expression and location, active
structural assumptions, and a fingerprint of the source, known input snapshots
and target facts. It does not expose arbitrary type candidates or invite new
constraints. Inputs not yet discovered are still checked when source resolution
requires them; a context fingerprint is not a proof of later obligations.

A response contains exactly three scalar fields:

```json
{"id":"the request id", "variable":12, "value":"7"}
```

The decimal string avoids host JSON-number precision limits. The response must
name an unfixed variable and one of its active values. It cannot supply source
code, commands, new relations, proof claims, or an instruction to prune a branch.
The problem/state fingerprints identify context; they are not cryptographic
certificates. Every proposed choice is validated against the current domains.

An accepted numeric proposal changes which variable is selected and which value
is tried first. A structural proposal changes the first value tried for the
requested decision. Every other value remains in the search. Propagation, contradictions,
rollback, observable-result uniqueness and budget accounting stay with the
solver. A poor proposal can increase the search cost or exhaust the budget; it
cannot justify a false success or a false impossibility claim.

## Failure, budgets and replay

`--max-proposals N` defaults to 32 requests per puzzle, shared by numeric and
structural search across accepted and rejected revisions. Backtracking does not
refund requests. Separate puzzles retain separate budgets. `--heuristic-timeout N`
defaults to 1000 milliseconds per call. Requests and responses are bounded to
64 KiB and parsed with a nesting bound. The solver terminates timed-out provider
processes and their process group. Missing executables, nonzero exits, malformed
JSON, stale replies, inactive values and oversized responses fall back to the
ordinary branch order. Diagnostics appear in the search report.

`--search-report FILE` saves requests, raw bounded responses, validation outcomes,
and decision/conflict/backtrack events even when a puzzle has no solution.
Its top-level `advice` array retains requests from discarded revisions as well
as the winning revision. These are records of work performed, not semantic facts
committed by the rejected candidate. Per-puzzle `search.proposals` remain useful
for inspecting the retained search graph.
`--heuristic-replay FILE` reads that report and matches the full request text.
It does not execute the provider. Unmatched or stale requests use the ordinary
heuristic. Corrupt report files are diagnosed. A provider and replay cannot be
selected together.

Exact replay expects the same source paths, solver build, schedule, problem and
options. It is a reproducibility aid, not a proof cache across changed inputs.
Changing a constraint invalidates the request context. A different search
schedule can change graph handles and therefore miss replay entries without
changing the language's result.

The current protocol guides finite numeric choices and finite choices that
determine types, shapes or other supported structural properties. It does not
yet accept model-generated source rewrites, choose representations outside those
encoded as finite choices, or run a background optimization service.
