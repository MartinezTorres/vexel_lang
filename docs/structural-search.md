# Decisions that change structure

Finite domain propagation handles arithmetic relationships efficiently when the
shape of the relationship graph is known. Vexel also allows a finite choice to
determine structure inside a closed solve region:

```vexel
answer = ?{
    n = ?(#u8, 1..5);
    data:#u8[n] = [7,8,9];
    n
};
```

Only the revision with `n=3` satisfies the array relationship. Neither an early
array type nor the first attempted candidate is published as the solve result.

## Checkpoints and closure

Before expanding a solve region, the resolver captures a checkpoint of the graph
and its binding/type/call caches. When an operation needs a concrete structural
property that still depends on a finite choice, it requests a decision. The
resolver forks the checkpoint for each alternative, supplies that assumption,
and uses the ordinary worklist rules to resolve the same region. There is no
second source interpreter for the candidate.

Each revision owns its relation/domain state. It can discover different types,
array sizes, invocations and dependencies. A rejected revision cannot contribute
its bindings, stores, type facts, constraints, cache entries or queued graph
handles to the winning revision. On commit, the worklist is reconstructed using
the stable checkpoint prefix and the accepted graph. External observers receive
a solve result only after every alternative has either been refuted or agreed
on the observable type and value. An unfinished proof refutes nothing and prevents
publication of a unique result. Two distinct completed results establish ambiguity
even when another alternative remains unfinished.

Valid alternatives may have different hidden layouts while producing the same
answer. Conversely, arrays of different lengths are distinct observable answers
even if they share a prefix. Contradiction, ambiguity, unresolved obligations and
exhausted budgets stay separate outcomes. Structural assumptions are discharged at the closed solve
boundary; they do not become permanent global restrictions.

Supported requests currently include array dimensions, range bounds, unrolled
repeat conditions, compile-time sorting, differing conditional types and
semantic failures confined to a symbolic branch. A nested solve cannot capture
unresolved parent choices. The explicit runtime-loop barrier still means runtime
control, and is not silently unrolled by this mechanism.

## Inputs and evidence

All revisions share an immutable snapshot of each input file's contents once it
is first read. Resolution cannot mix two versions of the same file across
candidate attempts. Dependencies needed to establish uniqueness across revisions
are retained even when they belong to a rejected alternative. An optional fallback
similarly retains observations which justify absence. Both mechanisms discard
speculative values and stores; neither may discard an input premise of its
accepted conclusion. Inputs used only by an abandoned type candidate, rather than
by the final proof, remain private to that candidate.

At quiescence, already established relations can refute an incomplete context.
A satisfying assignment to those partial relations cannot close its result.
Remaining finite decisions can trigger structural exploration to establish which
unfinished obligations are relevant. See the [closure contract](closure-contract.md)
for examples and the distinction from runtime parameters.

The report records structural assumptions, conflicts, unresolved alternatives and backtracks alongside
ordinary domain search. `--branch-order` affects both kinds of exploration.
Structural trials use the decision budget, the remaining resolution-step budget,
and a nesting bound of 32 assumed structural choices. Exhaustion never proves
that there is no solution.

The optional `--heuristic` provider can reorder the finite alternatives of a
requested structural decision. Its scoped request includes the solve expression,
active assumptions and the known source/input/target context. An accepted reply
changes exploration order; it cannot delete alternatives or establish a type.
Numeric and structural advice share one per-puzzle budget across revisions.
Requests from rejected revisions remain in the search report's `advice` array,
so offline replay can reproduce their work as well as the winning path. Invalid,
stale or unavailable advice leaves ordinary search available. See the
[provider protocol](search-provider.md).

The implementation uses whole graph copies. That makes isolation auditable and
exposes the semantics before optimizing persistence or incremental invalidation.
It can be expensive for many structural alternatives. It does not promise to
solve arbitrary recursive equations or an unbounded pure program; finite input
choices and explicit budgets define the experimental search contract.
