# Vexel architecture

The current closure boundary is enumerated in the
[four-pass acceptance checklist](closure-checklist.md). Source configuration,
boundary ownership and target realization use the contracts described below
and in the [current MSX guide](backend-msx-neo8.md).

Configuration annotations retain structured expression nodes as well as their
original text. Recognized facts and requirements demand ordinary source values
and add equations to the same request. Hard configuration witnesses participate
in observation closure; preferences affect realization. CLI contributions use
the same vocabulary and cannot overwrite source requirements. Unknown tooling
metadata stays inert. No separately evaluated build language is introduced.

Repeated streams turn result lifetimes into bounded runtime ownership. A shared
submission gate serializes execution; leases retain individual result slots
until release. Token exhaustion applies backpressure instead of aliasing stale
owners. This is a concrete boundary adapter alongside finite occurrence graphs.

The C projection can partition its closed control graph into bank continuations,
carrying locals through a target frame. Source recursion remains recursion;
ordinary branch and loop transfers become continuation edges. Initial project
realization can retry smaller groups after measuring SDCC object sizes. It
preserves candidate evidence and never rewrites an exported user's project.
Mapped globals similarly retain named ownership while target access routines
borrow a hardware window. A segment grant is an explicit adapter fact.

Stack analysis consumes emitted assembly and records its ABI, environment,
external-call and recursive-depth assumptions. Unsupported transfers leave an
open proof. Source resolution, successful linking and a proved target contract
are distinct reports: a C projection is conditional on the remaining linked
memory/stack checks. Measured workload maxima do not discharge stack bounds.

Vexel uses one semantic relationship graph to answer demanded observations and
construct admissible implementations. The supported frontend is the C++20
[relational solver](../experimental/relational/README.md), built as `build/vexel`.
It reuses the parser, source arena and numeric/resource helpers from `src` but
never links the earlier `Solver`. Older architecture ledgers describe prior
implementations; the [RFC](vexel-rfc.md) defines language behavior and the
[source contract](../experimental/relational/SOURCE.md) records executable limits.

## What is fixed, and what can change

A source input revision contains source files, static imports, resource snapshots
and supplied target/boundary facts. Lexical declaration membership is closed for
that revision. Lookup and negative capability answers can therefore depend on a
finite, complete candidate set. Function bodies introduce declarations only in
their own lexical scopes. Their later validation cannot extend a caller's set.

Closing membership does not resolve types, overload applicability, values,
shapes, effects or placement. Those remain relationships with dependencies.
Within an unchanged assumption context, propagation narrows possibilities.
Search explores genuine alternatives in reversible contexts. A candidate answer
is provisional until its relevant obligations close and its observation is
unique. Editing an input creates a new revision, so dependent answers must be
recomputed. No source meaning depends on declaration or worklist order.

Source evaluation order is different: stores, traps, completion and external
accesses retain their ordering. A known return value does not prove that its
computation terminates or has no effects.

## Representation and solver

An eager lexer/parser and inspectable syntax trees or IR are ordinary engineering
tools. There is no requirement to model lexical trivia as constraints. Their
purpose is to recognize structure and populate relationships; they cannot make
irreversible semantic decisions that should depend on later evidence.

The core implements structural terms, unification with occurs checking, finite
integer domains, arithmetic relationships, a dependency work queue and reversible
bindings. Shared obligation bundles and delayed builders construct additional
relationships when their declared inputs are available. Builders do not inspect
another candidate's binding store. Construction caches share relationships,
never a privately selected value or implementation.

An observation is a requested property: a type, field shape, constant value,
source activation result or physical implementation. An unobserved sibling may
remain unknown. A closed result requires both the relevant candidate set and its
obligations to close. An unfinished alternate cannot be ignored to manufacture
uniqueness. Internal assignments may differ when the observation is the same.

The implementation distinguishes resolved, contradictory, ambiguous, unresolved
and exhausted outcomes. Unknown facts and known runtime inputs are distinct.
Neither missing proof nor exhaustion establishes absence.

## Capabilities instead of semantic rollback

`?function(args)`, `receiver.?method(args)` and `receiver.?member` inspect the
available interface. Function/method probes use normal signature applicability
and specificity, without executing actual arguments or checking the winning
body. Member probes can use nominal identity and field names without demanding
unrelated fields. Ambiguity and unknown required types remain obligations.

An ordinary conditional guards the implementation. A selected body has normal
errors, effects, constraints and initialization requirements. It cannot be
rolled back because something deeper in the body is unavailable. The former
`#{ ... }` transaction machinery and fallible annotation traversal are removed.
Imports are static declaration contributions, not operations whose failure can
be caught by an optional block.

## Types, execution and recursive interfaces

Types expose independent identity, category, field, element, length and schema
properties. Structural type relationships can close without executing values.
Dependent shapes retain their value prerequisites. Finite cyclic interface
candidates still require validation against the original equations; a candidate
is not a source fact merely because a private request found it.

Execution projects the graph into ordered state transitions. Logical locations
and their reaching values stay separate from host pointers. Initialization has
both a proof and a physical readiness value: normalized proof formulas must
never become emitted guards that speculate through partial operations.
Function/loop boundaries consume their own return, break and continue exits.
Control simplification preserves unresolved integer relationships and handles
aggregate leaves independently. It must not require a complete value merely
to expose a part that other constraints can determine.

A runtime recursive edge refers to an existing interface promise. Base paths,
arguments and annotations can ground the result; an open cycle needs a return
annotation or another grounded relationship. Interface closure is not a proof of
termination. Known-input recursion still executes under its actual state.

Automatic captures distinguish type information, incoming values and writes.
Ordinary inference may establish a smaller interface. At a recursive capture
cycle the solver uses one conservative interface, not a search over omitted,
read-only and output-only subsets. If it cannot establish that interface, the
program supplies initialized/annotated state or explicit parameters/results.
This is a deliberate language restriction, not a fallback that guesses values.

Runtime specializations keep static facts alongside native slots. At a recursive
edge, a changing native constant becomes a runtime slot; invariant constants
remain available for fixed shapes. Changing types, shapes or non-native static
facts require an explicit fixed runtime interface. This prevents unlimited
specializations of an accumulator while preserving known string/shape inputs.
Frame reuse also records enclosing recursive interface assumptions. Different
contexts may get separate helper specializations; minimizing their count is an
optimization, while checking every back edge is a correctness obligation.

## Boundary and target facts

Sources and sinks define activation, effect and resource boundaries. Concurrency
is expressed through these graph contracts, not language thread or interrupt
keywords. Named begin/produce/release events describe bounded occurrence and
result lifetimes. Shared resources require compatible execution ordering;
static frame reuse requires proven exclusivity and closed feedback assumptions.

The backend contributes representation and storage constraints. It can choose
among behavior-preserving plans; cost cannot resolve observable source
ambiguity. Measurements belong to a particular artifact revision. Generated C,
assembly, resolved Vexel or another IR can be inspectable projections with
explicit assumptions; they need not form mandatory semantic phase barriers.

The current MSX projection exports an editable SDCC + megalinker NEO-8 project.
Per-function frames, bank modules and source/sink contracts are inspectable.
The target uses a 16 KiB baseline RAM budget. Explicit segment grants support
mapped persistent storage; bounded stream pools support repeated activations;
continuation cuts support functions larger than a bank. Stack bounds come from
conditional assembly analysis, separately from workload measurements. Operating
system allocation and arbitrary unbounded event ownership remain adapter work.

## Incrementality and optimization

For a fixed request, dependency wakeups avoid rediscovering unrelated
relationships. `SourceWorkspace` adds reuse across edits at whole-request
granularity. It retains up to 32 closed request groups by default. The exact
entry path and every `SourceOptions` field identify a group. Each answer carries
exact source/resource bytes, directory membership and path-selection facts,
including missing preferred paths, import file kinds and symlink destinations.
Validation compares content, not timestamps or hashes. A miss constructs a new
`SourceProgram`, with independent bindings, snapshots and physical decisions.

All source files in the parsed import closure support every request on that
entry. This is conservative: editing an unused body rebuilds those groups.
Resource support is demand-specific, so a palette edit can leave a tone-data
request valid. Shared search budgets and process-dependent answers bypass the
cache. Only resolved results are retained; errors and exhaustion never revive a
previous projection. Invalid entries are removed before rebuilding. Entries are
evicted by least recent use, and clearing the workspace releases all of them.

The CLI exposes this through `--session`. A `solve` line observes current inputs;
edits should finish between requests. Input reads are snapshots, not an atomic
filesystem transaction. Support is checked before reuse and again before a new
answer is retained. `--verify-incremental` independently solves reused groups
and compares complete reports and C/assembly projections. Report `stats` retain
the original derivation cost; `incremental.solver_work` reports new solving work
(zero on a hit), with verification work reported separately. Revision numbers
identify group derivations within that workspace, not a global filesystem clock.

`SourceProgram` and `--then` still retain a fixed source snapshot. Fine-grained
relationship reuse, reuse across process restarts and splitting jointly
requested roots into independently cached components remain future work. See
the [session contract](../experimental/relational/SOURCE.md#incremental-sessions)
and `make incremental-msx` for an edit replay and measured target workload.

Compiled pure kernels, backend heuristics or an LLM may propose faster ways to
solve a region or better valid realizations. Proposals must cross the same
validation boundary as any other candidate and be reproducible from recorded
inputs. They cannot define semantics or conceal an unresolved obligation. These
services are future work for this frontend; the relational core and source
semantics do not depend on their availability.

Validation checks observations, rejected programs and generated execution.
Tests for retired semantics are replaced by migration/contract tests. Independent
oracles and changes of solver order are useful; repeatedly counting assertions
or proving implementation details is not an acceptance criterion.
