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
realization can retry smaller groups after measuring SDCC object sizes or a
guarded compiler resource rejection. Every emitted module identifies its original
function, partition role and effective continuation limit. Measured failures
tighten that owner's scoped configuration preference; unrelated functions keep
their policy. Input fingerprints delimit the lifetime of this feedback, and
changes between attempts stop realization. The loop preserves candidate evidence
and never rewrites an exported user's project. One whole-program solve therefore
does not imply one SDCC translation unit or one hardware bank.
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

Observation closure canonicalizes private identities when comparing meaning.
Generated C can still retain discovery-specific names for loops, temporaries
and internal helpers. Different worklist schedules can therefore produce
different file hashes without changing behavior. Artifact fingerprints identify
one recorded implementation; behavioral checks across schedules establish a
separate property. They must include ordered effects as well as returned values.

Source evaluation order is different: stores, traps, completion and external
accesses retain their ordering. A known return value does not prove that its
computation terminates or has no effects.

The shared graph carries several kinds of evidence. Their scopes determine
which transformations are justified:

| Evidence | Scope | What it can justify |
| --- | --- | --- |
| Value equality or range | Owner, observation time and branch/input premises | Smaller representations, constants and less result transport |
| Completion and absence of effects | Executed trace, including actual arguments and unobserved results | Removing an unused invocation |
| Initialization or loop invariant | Established entry and every relevant transfer | Removing readiness storage or sharing carried values |
| Activation nonoverlap | Boundary ownership and outstanding-result lifetimes | Reusing fixed workspace instead of stack storage |
| Target size, stack or timing evidence | Exact generated inputs, tools and measured artifact | Accepting placement or choosing among valid implementations |

These facts can enable one another without becoming interchangeable. For
example, proving a helper always returns its argument removes return transport;
removing the call additionally requires its execution to be discardable. A
backend may then choose fixed workspace when activation lifetimes permit it,
and assembly analysis still has to discharge any required stack bound. Cached
proofs keep their premises and revision; measured feedback keeps its artifact
identity. Neither a search heuristic nor a favorable measurement supplies a
missing semantic proof.

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

Immutable closed terms contain no variable that a later binding can change.
Occurs checking skips those subgraphs, and clause instantiation shares them
instead of copying them. Open structure still follows the current reversible
binding store; the shortcut does not weaken cycle checks or freeze unresolved
promises. This distinction also keeps a shared ground DAG from being expanded
once per path merely to prove that it contains no variables.

Open terms retain that sharing too. Comparison, occurs checking, unification,
clause matching and dependency subscription visit distinct nodes or node pairs;
substitution and freshening rebuild a shared subgraph once and retain untouched
structure. Their memos belong to one operation and binding view, so neither a
rollback nor another clause instance can reuse a privately resolved answer.
Unification keeps its original left-to-right constraint order and newer-to-older
variable aliases. Retained roots protect pointer lookup from allocator reuse.
The shallow scalar cases avoid allocating a traversal memo. Pair comparisons
keep their first eight exact pairs inline and promote larger traversals to a
hash set. Clearing a shallow clause probe therefore allocates and frees no hash
nodes; large shared graphs retain the same complete duplicate detection.
Binding values use an exact hash lookup by variable identity. Ordered patches
and touched-variable worklists still determine propagation order; changing the
lookup container does not choose a different proof schedule.

Equal-cost realization ties retain the historical lexical text order, but the
comparison streams that text and skips structurally equal ground subgraphs.
An enormous shared prefix therefore does not need to be expanded just to
compare two final placement choices. Delimiters and lexical integer ordering
remain unchanged; this is an implementation cost change, not a new preference.
Complete term rendering uses an explicit traversal and preserves its text.
Automatically generated finite-domain labels are diagnostic prefixes capped at
256 bytes plus an ellipsis. Two alternatives may therefore have the same
display label while their complete term heads remain distinct. This keeps
naming a choice from expanding an exponentially shared graph before solving it.

Delayed-builder and numeric-control caches use exact structural term identities.
An index records constructor kind/name, exact integers, variable identity and
the identities of child terms. Repeated DAG children therefore stay shared
instead of being expanded into text for every lookup. Pointer lookup is only a
fast path: independently allocated copies receive the same structural identity,
and each address observation checks a weak owner before reusing its identity.
Expired observations are periodically swept; the exact canonical key remains,
so releasing a transient term does not change its identity if it appears again.
Forced address reuse is checked explicitly. The index stores no binding state,
assumptions or solved answers. A
resolved substitution is a different term and is indexed separately. Identities
are private to their index lifetime and are never persisted or exposed as source
meaning. Human diagnostics and the control proof's BDD atom ordering retain their
existing text representation.

Each term factory reuses its immutable integer zero and one. The cache owns
only those two terms; variables remain fresh, and no binding, assumption or
proof answer is shared through it. Broader small-integer caching was measured
and rejected because it added lookup work without a useful memory saving.

The Boolean proof stores its exact node and operation triples in contiguous
append-only hash tables. Collisions compare all three identities; hashes never
stand in for proof equality. Quantification, substitution and term rendering
use operation-local node memos bounded by the original root identity, since
every child predates its parent. Atom classification is shared across uses
within a quantification, but not between different removal predicates. These
storage choices preserve lexical atom ordering and the existing proof budget.
Unique-node insertion returns the existing identity or interns the next node
in one lookup. At the budget ceiling it still permits reuse of existing nodes
and rejects only a genuinely new node.
Small memo tables grow fourfold to avoid repeated rehashing. Once capacity
exceeds the proof's node-budget scale, growth returns to doubling to limit
unused capacity in large operation caches. This allocation policy changes
neither the node budget nor exact key comparison.
The stored triples and results use 32-bit identities, justified by that
100,000-node budget; public proof handles keep their existing type. AND/OR
operation keys share reversed operands, and selected branches use the exact
identities `select(c,c,x) = select(c,1,x)` and
`select(c,x,c) = select(c,x,0)`.
Constant-bit equality, parity and ordering avoid constructing unused Boolean
operands. Wrapping addition does not construct its discarded final carry, and
a primitive spanning its full native domain needs no clamping relation. These
changes remove undemanded proof work without changing the represented values.

A Boolean handle also carries a complement bit. A function and its negation
share the same physical nodes; negation toggles that bit. Interning normalizes
the low edge, and the 100,000-node limit counts physical storage. Quantification,
substitution and term reconstruction interpret the complement on both outgoing
edges. In particular, existential quantification of a negated function still
quantifies its negated branches. The truth-table checks cover both polarities
and simultaneous substitutions. Atom ordering and source identities are
unchanged, while smaller graphs can complete more proofs within the limit.

Atom identity is structural too: a word bit is identified by its exact source
term and bit position, independently of its printed name. Small names retain a
bounded display for fast ordering; larger names are compared as streams that
skip equal shared subgraphs. This preserves historical lexical ordering without
expanding a compact DAG into enormous text. Distinct structures that print alike
remain separate atoms, with a structural ordering tie-breaker. Loop renaming
uses primitive bit handles scoped to the same proof graph. The same streamed
comparison implements realization ordering; bounded displays are never equality
keys.

Completed optional conditional refinements can be reused within one source
session. The key includes the exact value, the ordered premises, and whether
native interpretation is enabled. Every interpretation used by a retained
answer is recorded and checked again in the same order before reuse. A changed
interpretation invalidates the answer. A failed or exhausted attempt publishes
no answer, and exhaustion while checking a dependency returns the original
residual value. This cache does not replace mandatory initialization or
completion obligations. It retains at most 4,096 answers and 65,536 structural
identities between top-level queries; clearing is deferred until nested queries
return, so an outer key cannot refer to recycled identities. These are retention
limits, not a hard bound on the memory needed for one proof.

Optional queries can also share Boolean subgraphs. These graphs contain pure
value functions; every query still constructs its own premise conjunction.
Two depth slots keep nested interpretations separate from a live outer query.
If retained work fills a graph, the query retries once with a fresh graph before
reporting exhaustion. An exhausted interpretation is not retried as a graph
capacity problem. Between queries, a graph is released at its 100,000-node
ceiling, above 65,536 structural terms or above 262,144 operation-cache entries.
Thus constants and memo entries cannot accumulate indefinitely even when they
create few Boolean nodes. Exact interpretation revalidation remains necessary
for completed answers; shared subgraphs do not install assumptions or cache a
failed answer. Reports distinguish constructed nodes, graph reuse and fresh
budget retries.

Comparisons can observe a conditional constant graph directly. A bounded scan
of at most 256 distinct choice/value nodes verifies that every leaf is a native
constant. The comparison maps those leaves to Boolean outcomes, applying its
actual width and signedness. Equal outcomes need no proof of the condition that
selects between them. Thus a threshold on a small result code need not expand
arithmetic that affects only a lower-valued alternative. Unsupported leaves use
the ordinary word proof; omitted predicate reasoning does not erase execution,
checks or traps from the source trace.

Native result certificates also distinguish construction from solving. One
physical call slot fixes its argument, receiver and capture snapshots. Its
shadow request can therefore retain the constructed obligations across retries.
Each retry runs a fresh solver with fresh bindings, the current parent-fact
checks and the original proof barriers. Unfinished requests are
not negative answers. A successful certificate still describes only the call's
values: its runtime invocation, captured writes, traps and completion remain in
the execution graph. Keeping the request avoids rebuilding identical private
cells when a bounded certificate cannot yet close.

Ordinary attempts allow 20,000 work units. Attempts numbered 2, 4, 8, 16 and so
on receive larger budgets, doubling up to 160,000 and always respecting the
caller's lower limit. Other retries keep the ordinary budget. This sparse
schedule lets a repeatedly requested proof finish without making every failed
attempt expensive; its aggregate allowance stays below 2.5 times ordinary fuel.
Retry history records effort only. Each attempt still sees current facts, and
neither an exhausted result nor a previously absent input is cached as a fact.
Reports expose total native work and the number of boosted attempts.

When a bit-level control proof exhausts its node budget, observation boundaries
can try a separate bounded word-algebra certificate. Addition, subtraction and
multiplication have sparse polynomial normal forms modulo the native word size.
This can establish identities such as `a*b == b*a` and
`a*(b+c) == a*b+a*c` even when their Boolean expansion is too large. The
certificate preserves wrapping and signed interpretation: low bits may pass
through a wider operation, while a narrower wrap stays an opaque atom. Equality
requires compatible, injective representations of both values. Exact term
identities distinguish atoms, including distinct call occurrences. The attempt
allows 4,096 work steps, 64 monomials and 16 factors per monomial; exceeding a
limit supplies no certificate. This proves value relationships, not the
existence, initialization or successful execution of their operands.

The same algebra supplies value classes at physical interfaces. A function's
equally typed results can share a slot even when their expressions differ
syntactically. Loop classes still need an initialized entry anchor and must
survive every transfer in the existing simultaneous induction check. Conditional
values have a proof form `f + truth(c)*(t-f)`. Truth atoms are distinct from raw
integer atoms and have idempotent multiplication; the expansion never becomes
emitted arithmetic. Narrower wraps remain opaque here too. Each class belongs
to one function return or loop clock, so it cannot equate independent calls or
replace an earlier snapshot with a later read.

Constant and single-atom reductions also feed the shared value facts. An alias
such as `(n+37)-37` retains its word conversion and can become an immutable
formal alias after the ordinary type checks. This lets it cross materialized
calls and participate in loop invariants without copying a computation into a
caller. Only discardable value work is reduced; producing executions, readiness
and traps retain their original trace. Exact algebra reductions are cached by
expression structure, independently of range or induction hypotheses. Range
analysis uses the same term-identity index while retaining its own environment
and hypothesis boundaries. Algebra's polynomial representation is a certificate
format, not an instruction-selection strategy.

An observation is a requested property: a type, field shape, constant value,
source activation result or physical implementation. An unobserved sibling may
remain unknown. A closed result requires both the relevant candidate set and its
obligations to close. An unfinished alternate cannot be ignored to manufacture
uniqueness. Internal assignments may differ when the observation is the same.

The implementation distinguishes resolved, contradictory, ambiguous, unresolved
and exhausted outcomes. Unknown facts and known runtime inputs are distinct.
Neither missing proof nor exhaustion establishes absence.

Before physical demand closure, the C projection establishes a small set of
immutable value facts. Native result summaries may name a constant or an
unchanged formal input. At each call, a formal alias can replace return transport
only if its actual value is also immutable after the argument conversion. A
mutable loop cell or a preceding call buffer remains a snapshot: reading it
later would change the meaning. This does not inline computations across `&~`.

Loop facts use simultaneous induction. Candidates must have an established
entry value and preserve it through entry, condition and body transfers,
including storage conversions. Failed hypotheses are removed and their
dependants rechecked until the remaining set is closed. A value invariant also
requires proven readiness; zero-filled uninitialized storage is not a logical
value. Always-ready slots need no physical readiness flag. Result summaries grow
only from established facts, then may help prove further loop invariants. An
unanchored recursive cycle supplies neither a value nor a completion proof.
These finite proofs run in `source_facts.cpp`; speculative hypotheses never enter
the selected semantic model. The scalar proof algebra is deliberately limited;
failure to prove an identity keeps the original representation.

Equality facts can also relate values that change. `source_equalities.cpp`
groups equally typed loop leaves by established entry equality, then assumes
those groups while checking entry, condition and body transfers. A differing
transfer splits a group; all dependent groups are rechecked until no further
split occurs. Only always-ready leaves with total transfers are eligible. The
remaining classes can share a carried cell and its parallel-transfer staging
cell. The equality belongs to that loop's state, not to another observation time.

Native results and returned captures can likewise share a member when their
converted, total expressions are equal at the same invocation's return. These
result classes grow by union, without recursive equality hypotheses. They can
propagate through materialized forwarding calls and enable loop equality;
proved loop equality can in turn establish more result classes. This closure
alternates with immutable-value and readiness proofs before demand is computed.
A representative has the same logical scalar type as its aliases. Different
call occurrences remain distinct snapshots, even when their callees and actual
arguments match. Calls, checks and completion traces remain executable.

The ordinary C projection closes one dependency graph over arguments, results,
incoming and returned captures, loop-carried leaves and initialization flags.
`InterfaceDemand` takes selected execution traces and observable roots. A
demanded result requires its producer's inputs; a required formal input requires
the corresponding actual values at every caller. Carried loop state requires
its entry, condition and body transfers, guarded by readiness. These edges meet
in one finite work queue, including recursive call groups and loop cycles. A
cycle that only forwards an unobserved value cannot create demand for itself.
Formal identities include their owning function; call and loop occurrences
retain their separate identities. Declaration order cannot change the closure.

`source_ranges.cpp` adds native integer envelopes to this shared physical view.
Every materialized function receives the union of all its callers' converted
argument/capture ranges, and supplies result ranges back to those callers. Loop
state combines its initialized entry and all changing transfers. An arm that
retains exactly the same cell preserves its envelope. Each changing transfer
can use its own guards; those conditions are not assumed to remain true later.
Readiness must already be proved before narrowing
a loop cell. Distinct native specializations retain distinct interfaces.

The analysis starts with valid native domains and refines them in synchronous
rounds. It models wrapping conversions, addition, subtraction, multiplication,
bitwise operations, comparisons and selections. A wrapping interval that crosses
the representation boundary falls back to the full domain. Division, shifts,
floating/fixed arithmetic and unfamiliar expressions remain conservative.
Each intermediate envelope is valid: after 32 rounds, remaining imprecision
keeps the general code and `value_ranges.converged` reports false. Recursive
result envelopes describe successful returns and never prove termination.

Function boundaries also expose a reusable abstract transfer: the selected
result value graph can be queried with a particular invocation's argument and
capture bounds. `callRange` evaluates that graph in an isolated environment,
including queries through nested native calls. The ordinary union of all
callers still governs the shared function body and its private interface. A
wide caller therefore need not erase a narrower caller's return facts, and
successive captured-state updates need not introduce a false recurrence by
merging their distinct invocations too early.

Contexts are cached by function identity and converted input envelopes. The
cache stores analysis results, not runtime results: calls, callbacks and
validation still execute. Equal envelopes never establish value identity.
Each call occurrence remains its own result snapshot; contextual reasoning
does not replace it with a later read of a mutable argument or capture.
These facts feed the same constant, demand, loop and storage closures as
global ranges, so their benefits reach every C projection consuming that view.

Queries reuse the existing graph without creating specialized function bodies.
They currently carry intervals without general correlations between independently
varying values. Foreign replies retain their established conservative envelopes. Recursive
queries fall back to the globally valid return envelope. Each refinement round
allows 256 cached function-input contexts, 16,384 uncached contextual value queries and
32 nested call queries; the final reporting pass gets a fresh budget. Exhaustion
retains general facts and is reported independently of range convergence.
`value_ranges.call_results` lists per-site return bounds tighter than the shared
function's bounds, and `value_ranges.contexts` records limits and budget hits.

Selections extend the same query environment with branch-local assumptions.
Integer comparisons refine both operands, equality intersects their envelopes,
and inequality can exclude a singleton endpoint. Negation, conjunction and
short-circuit selections can establish a path when the opposite arm cannot
produce the required truth value. Two possible paths remain conservative;
the analysis does not enumerate arbitrary disjunctions. Each arm has a distinct
fact environment, and its facts expire before the sibling arm is evaluated.
The combined result envelope can then cross calls and select private storage.

Facts match structural expression identity within the current owner, including
call and loop occurrence IDs. Equivalent rebuilt terms can therefore share a
fact, while separate snapshots remain distinct. A constraint on a converted
value flows back to its input only when that conversion preserves numeric value
throughout the input envelope. Narrowing or signed reinterpretation must not
silently constrain the original value. Likewise a guarded loop transfer uses
facts at that transfer's observation time, never as an unconditional invariant.

Branch queries share the contextual term budget and allow 32 nested environments;
`branch_depth_limit` and `branch_depth_peak` expose that bound. Budget exhaustion
retains previously justified envelopes. `examples/conditional_facts.vx` demonstrates
a guarded result becoming byte-sized even with an unrestricted 32-bit caller.
Only the resulting envelopes are published: branch assumptions do not mutate
the selected semantic graph or erase its execution obligations.

The same branch model also describes a native function's incoming domain.
Discovery records every invocation occurrence with the execution predicates
that enclose it. Each refinement round bounds its actual arguments and captures
under those predicates, then unions all contributing occurrences and callers
for the shared function. Forwarding helpers participate in that closure. A
guarded byte-sized input can therefore shrink a shared private argument even
when the public entry accepts every 32-bit value. An unguarded caller still
contributes its entire domain, and a predicate after a call cannot constrain
that earlier invocation.

Loop conditions require separate observation times. Calls in the condition's
execution trace occur before the truth test; only body calls inherit the true
condition. Their actual expressions include any updates made by the condition.
At loop entry and after a possible loop execution, enclosing facts over mutable
call/loop snapshots are discarded. Predicates over immutable incoming parameters
may survive. This is a conservative proof boundary, not a language restriction.

`value_ranges.call_inputs` records guarded sites, occurrence/path counts,
argument/capture envelopes and whether all recorded paths were excluded by
established facts. An excluded site contributes no input values; if a function
has no contributing calls, its existing envelope remains unchanged. This does
not erase execution or prove completion. Queries retain unconditioned bounds
when guards are unsupported or budgets run out. Paths retain at most 32 guard
premises, with omissions reported through `contexts.budget_hit`. The result feeds
the existing private storage, demand and C projection policies without cloning
functions. `examples/guarded_inputs.vx` demonstrates this through two helpers.

Proof queries share a structural value graph within the selected program.
Analysis-local expression IDs distinguish constructor kinds, integer values,
variable IDs and recursively identified operands; they do not use printed
spelling or treat matching envelopes as equal runtime values. Call and loop
occurrence IDs remain part of those operands. General answers are scoped by
function owner. Contextual answers additionally use an interned immutable
environment containing argument/capture envelopes and a sorted map of branch
assumptions. Equivalent rebuilt expressions and repeated result-field queries
can therefore reuse an answer without visiting its transfer rules again.

Adding a branch assumption selects a new environment, leaving sibling answers
intact. Every global refinement round clears environments and answers. The
readiness induction checker can also install and reject temporary hypotheses;
changing that hypothesis set invalidates cached answers without replenishing
the contextual work budget. This prevents speculative facts escaping their
proof while permitting established readiness to simplify later projections.
The cache contains abstract proofs, never executable results or transformed
function bodies. Unsupported queries still retain conservative envelopes.

`contexts.node_peak` measures the largest number of contextual query evaluations
in one budget period; `environment_peak` counts distinct branch/input environments
in one cache generation. `query_hits` and `query_misses` accumulate nonliteral
query lookups and evaluations across the projection, including general queries
and final simplification. Cache hits do not consume the contextual node budget.
`examples/shared_proofs.vx` demonstrates reuse across guarded record-like result
fields with an unrestricted public caller. These counters describe analysis
work, not runtime performance or the cost of the complete source solve.

Proved singleton expressions feed the existing constant/alias/demand closure.
This can eliminate a predicate result and its transport through several calls,
or remove a branch whose condition is total and determined. Checks and producing
calls retain their execution obligations. Source entry validation uses the raw
input, so a declared range cannot justify deleting the check enforcing it.
Ordinary, pure-value, fixed-binding and region projections consume the same
facts, retaining their own ABI and storage policies. No new language syntax or
narrower declared type is required.

`source_transport.cpp` uses those envelopes to choose private scalar storage.
Logical argument, capture and result types remain separate from their storage
types. An integer leaf may use an 8- or 16-bit C scalar when its entire proved
domain fits and that reduces its size. Negative domains use signed storage;
nonnegative domains may use unsigned storage even for a wider signed source
type. Boolean, floating and fixed-point representations are unchanged.

The same choice controls native prototypes, result structs, caller snapshots,
loop cells, banked continuations and frame-budget accounting. Reads use the
actual storage width and restore signed values; arithmetic keeps its original
logical width. The alternate region projection consumes the same carried-state
layout while retaining logical leaf indices. Public source arguments, explicit
C binding inputs/results, foreign reply buffers and persistent storage keep
their existing contracts. No narrower private slot weakens their checks.
`runtime_functions.transport_types` and each loop's `transport_types` expose
logical/storage pairs. Loop byte counts describe one retained state image;
ordinary C may need both current and next images, and target stack use still
requires backend evidence.

This is one implemented instance of sharing information across a boundary:
the consumer supplies demand, the producer supplies dependencies and execution
requirements, and the C realization chooses transport. Original semantic terms
remain unchanged. An unused expression can disappear only when a conservative
scalar check proves it total and effect-free. Potentially trapping actual arguments retain their
positions in the call interface; potentially trapping loop transfers retain
their slots and readiness guards. An unused result with unproved totality is
still evaluated without transport. Foreign reply buffers and validation retain
their existing ABI. No physical demand reduction bypasses semantic resolution.

`source_execution.cpp` adds a separate completion/effect certificate for private
functions. It accepts finite sequences and branches of discardable evaluations,
and calls whose actuals and callees are already certified. All logical return
and capture leaves participate, including untransported results. A work queue
closes this fact from finite anchors through the call graph; recursive cycles,
loops, foreign calls, storage, checks and unknown operations cannot certify
themselves. Value equality alone never supplies this certificate.

Demand distinguishes each invocation's results from the union required by its
callee's shared interface. When an invocation has no observed result and both
its execution and actual evaluations are certified discardable, it becomes
`Done` in the physical view. Another caller can still require the same function.
Both ordinary C and alternate value/region projections consume this fact, and
frame/bank planning sees the resulting call graph. Logical numbering and the
selected semantic model remain intact. Reports expose `discardable_execution`
per function and the number of `discarded_calls`. Formal-input demand remains
conservative across callers; this does not claim a globally minimal program.
The [execution-facts example](../examples/execution_facts.vx) shows a removable
pure invocation alongside an observable sink and a required possible trap.

Value discardability and stability across a loop traverse each shared immutable
node once per query. Their local worklists avoid recursive expansion of every
path through a DAG. These traversals introduce no persistent purity cache and
no new semantic rules. Immutable expression bounds can establish a nonzero
divisor or a shift count within the operation's width, including masked counts
and divisors made nonzero by bitwise OR. The divisor is converted to its actual
arithmetic width before checking; a nonzero word can become zero as a byte.
Signed minimum divided by minus one wraps under Vexel's integer semantics.
Every operand must separately be discardable: a safe outer division cannot
erase a trapping calculation of its numerator. Uncertain conversions and
unfamiliar value operations retain evaluation; changing snapshots still do not
become loop-invariant premises.

Completion queries also consume the closed caller/capture and loop envelopes.
`discardable_results` records each private logical result's certificate; demand,
calculation-frame planning and C emission consume that same decision. A result
such as `1000/d` can disappear with its private argument when every incoming
`d` is nonzero, while an additional zero-capable caller prevents that proof.
Lazy value arms use the existing branch range environments with bounded work;
the condition itself and every reachable operand still must complete. These
proofs never certify a snapshot's producing call, a foreign effect, a recursive
cycle or a loop's termination merely from its successful-value range.

Constant division is another physical realization of the certified integer
graph. The divisor is converted at the operation's width before checking that
it is nonzero. Powers of two retain shift/bias formulas, including negative
signed divisors. Other constants produce width-specific, single-evaluation
leaf helpers using binary subtraction thresholds. Signed helpers operate on
unsigned magnitudes, so the minimum signed input is representable. The bounded
finite-table search can choose ROM for the same graph, and ordinary C uses
these realizations even when no shared scratch was needed. The table heuristic
accounts for the expanded arithmetic work; measured target costs still decide
whether a particular alternative is worthwhile.

Loop-initialization closure uses the same ownership rule for its stable-slot
substitution, numeric dependency discovery and changing-value classification.
Their memos live inside one resolved loop context and retain graph sharing.
Dependency discovery remains a fixed point over the required slots. The entry,
back-edge and read-safety relationships are unchanged. Source-leaf expression
previews in the report are bounded and explicitly marked when shortened, so
diagnostic rendering cannot undo this sharing by expanding every printed path.

Only after closure does the C realization remove unused arguments and carried
leaves, and assign retained results to native-width struct members. A byte result
needs one byte instead of a four-byte generic slot. Completion-only native calls
have no result pointer. Bank continuations retain these typed locals and
parameters; static frame selection and assembly stack analysis see the resulting
interfaces. The packaged MSX plan carries the logical-to-physical mappings and
demand report alongside the placement inputs.

The demand closure now carries a finite width lattice on each logical leaf:
absent, low byte, low word or full word. Identity edges connect invocation
snapshots to shared function results; expression transfers connect results,
arguments, captures and loop state to their producers. Stronger requirements
revisit those transfers, so recursive callers and backedges reach the same
join independently of discovery order. A shared expression memo is indexed by
owner, immutable term and required width. There is one closure for liveness and
bit demand, rather than a separate Boolean pass followed by local guesses.

Modular operations and casts propagate low-bit requirements. Constant AND/OR
masks exclude overwritten bits. Fixed shifts and unsigned power-of-two division
retain the contributing input slice, rounded up to a native carrier. Signed
remainder permits the numerator's low bits only while the observed slice is
within the divisor's power of two; larger observations require its sign.
Unknown operations and full-value tests are conservative barriers. Possible
traps remain consumers independently of the result's required width.

`planStorage` combines this demand with the previously established value
ranges. Range-based storage can reconstruct a full value; low-bit storage is
an unsigned residue and introduces no new logical range fact. Explicit casts
at private arguments, results and loop transfers expose the smaller result to
the calculation scheduler. A cast around a conditional transfer is distributed
into its arms, preserving lazy evaluation and avoiding reads of inactive
snapshots. Alternate region projections consume the same loop layout, while
their mutable transfer scheduling retains its own ordering constraints.
The [bit-demand example](../examples/bit_demand.vx) exercises the call/loop
relationship; `demanded_bits` reports its logical-leaf requirements.

There is one union interface per physical function, not a specialized interface
per call site. Loop-return payloads remain conservative consumers. The graph
does not yet combine demand with bank placement, lifetime allocation, foreign
behavior or measured cost choices in a general shared fact system. These remain
distinct questions; a logical value, its execution prerequisites and its chosen
physical representation must remain distinguishable as the system grows.

A closed execution graph can admit more than one C interface. For bounded,
total integer graphs, the optional value projection keeps native widths,
shares computations and writes a caller-owned typed result. It certifies the
execution trace as well as the returned expressions: an unused division, an
effect or a possibly nonterminating call cannot disappear merely because its
result is unused. Unsupported traces retain the ordinary projection.
When a result leaf selects between alternatives, a second interface exposes its predicate
and its specialized cases with explicit input dependencies. An adapter may
acquire only the needed inputs for the selected case, preserving one coherent
snapshot and introducing no intervening effects. These physical boundaries
come from the complete graph, not independently resolved source fragments.
Invariant result leaves and returned predicates can accompany that shared
guard. Each case specializes the whole result graph, then derives its input
list. Local Boolean implications can remove further dependencies: the false
case of `a || b` establishes both `!a` and `!b`. These facts belong to that
case's snapshot; they are not global input restrictions. The first selected
result leaf determines a stable partition; other result guards may remain
independent and keep their still-needed inputs. The branch inspector defines
the partition, so an embedding must not infer it from source spelling.
Restricted input domains retain the full interface. If the
optional case proof exceeds its construction budget, the already certified
full interface remains available.
Within a factored guard, independent result leaves are written once and retain
their original field identities. Already-protected inputs need no duplicate
snapshot; shared calculations keep stable temporaries. Branch-local C scopes
allow the target compiler to reuse mutually exclusive storage.
Comparison bounds are reused within each pure branch, including reversed and
signed comparisons with different constants. A frame projection moves an
otherwise independent result into the branch only when both alternatives
become constants; broader movement increased measured SDCC spills. Ordinary
direct value functions can use the complete specialization. Algebraic equality
does not by itself choose the fastest physical form: replacing Boolean value
selections with their predicates was rejected after it lost shared branching
and increased target time.
When activation analysis also excludes overlap and open boundaries, a fixed-RAM
alternative places inputs, shared calculations and results in a named frame.
The embedding selects this interface and budgets its RAM; it owns the frame
from the first input write through result consumption. Assignments execute on
every invocation. Results needed across another invocation must be copied to
distinct storage. This preserves pure source semantics while avoiding the
parameter/result-pointer ABI and stack-addressed temporaries on Z80.
Named C bindings can now delegate that choice to the solver. Module metadata
supplies relative frequency and the external engine's lifetime contract;
activation analysis still has to prove exclusion inside the resolved graph.
Native build choices can contribute `binding_frequency.IDENTITY` preferences.
These cap the declared estimate; zero keeps an alternative callable with an
automatic frame and reserves no fixed RAM for it. The selected program records
the effective frequencies alongside its ownership proofs and placement choices.
A deterministic budget policy chooses fixed or automatic frames and emits the
same declaration/input/result interface for both. One generated C module owns
all selected fixed storage, so including a header in several callers does not
duplicate RAM. Explicit `c_disjoint` facts permit storage sharing only when
every pair in a slot has disjoint complete input-through-result lifetimes.
Exclusion is not transitive and cannot override an open activation graph.
Typed C unions implement the slots, with target size assertions. The plan
reports each decision, slot and physical RAM cost; this is not a whole-engine
stack bound or an inference of arbitrary C lifetimes.
See the [C value interface](backend-msx-neo8.md#pure-value-interface).

The pure realization also searches for small finite interfaces inside the value
graph. A computed byte that dominates every nonconstant path to a consumer is
a sufficient input for that consumer, even when its producer combines several
source parameters. Exhaustive evaluation of all 256 bit patterns can replace
the remaining arithmetic with immutable bank-local ROM. The producer remains
executable and no source address escapes. Dependency intersection establishes
the cut; exhaustive evaluation establishes the replacement; bounded work and
ROM heuristics decide whether to use it. These are separate obligations.
Global and binding-scoped `value_table_bytes` and `value_table_search`
preferences let target measurements restrict this search without changing the
source equations. Mutable regions retain their ordered effect realization.

An execution-region binding extends this ownership interval over ordered sink
calls and their continuations. The typed region projection handles integer
graphs, structured loops and synchronous boundaries; it keeps live region
storage distinct from bindings a callback may activate. Readiness invariants,
control-flow facts, interference-based state reuse and parallel-copy scheduling
reduce the frame without changing the execution trace. Decisions precede the
state writes they control; an old outer-loop value stays live if selecting an
inner result still needs it. Scratch sharing is local to effect-free transfers.
Boundary result definitions also delimit snapshot lifetimes. An internal name
can borrow a published result until another definition would destroy a needed
value. Copy equality permits two names to share storage while both are live;
later definitions, entry values and parallel writes constrain that sharing.
Truth-only selections become control-flow edges before the final liveness pass,
retaining narrowing, numeric results, short-circuiting and the ordered boundary
trace. These transformations use the region's explicit data and effect graph;
they preserve fresh engine reads and the target's operand safeguards.
Unsupported traces retain the ordinary backend and fail an explicit region
request rather than losing effects. `source_region.inc` implements this
realization alongside the pure value emitter; `source_region_flow.inc` contains
its control-flow and storage analysis.

Native boundary implementation is now a separate physical choice. An adapter
offers a named C statement macro alongside the existing footprint and feedback
contracts. Region emission either expands it at the ordered call position or
retains the wrapper, according to global/per-symbol preferences. Both choices
use identical typed publication fields, checks and snapshot lifetimes. The plan
records availability separately from selection. No code motion, read caching,
residency or lifetime proof follows from the offer. The adapter is responsible
for equivalence and its native layout and mapper operations; an unavailable
selected macro fails compilation. This provides a measured alternative without
teaching the solver game-specific C structures. It is not yet a solver for
arbitrary native layouts or estimated boundary costs.

The same final control-flow graph now allocates complete native publication
objects. After scalar coalescing, backward liveness includes replies retained
across later calls and loop state stored in argument fields. A conservative
conflict graph also separates objects read and written by the same parallel
transfer, avoiding physical aliasing that the scalar copy scheduler does not
model. Deterministic size-first packing places compatible objects in typed C
unions; it is a feasible allocation, not a minimum-storage proof. The plan
records slot membership, conflicts, shared bytes and the separate-object cost.
The default cost heuristic keeps directly expanded native objects dedicated:
exposing their aliases to SDCC increased measured spills in the world region.
It records this choice separately from proven conflicts. A preference can
retain all separate objects or permit all compatible sharing for measurement.
This works within automatic frames as well as fixed frames. It does not extend
the region's activation exclusion or allow callbacks to borrow its live storage.

Pure predicates inside a region can also acquire their own physical function
boundary. The backend knows which fields are live at the decision and that
the selected expression performs no stores or callbacks. A small generated
function can therefore read those fields as immutable inputs for that call,
avoiding scalar argument publication to generic SDCC arithmetic helpers.
The calculation stays at the original control-flow position and reads fresh
values on every invocation. An automatic occurrence passes its own frame;
fixed occurrences use direct addresses. The initial expression family and
number of definitions are bounded, with a configurable fallback and explicit
function/call-site counts. Storage allocation and source effects are unchanged.

The MSX backend also has a bounded measured-selection utility. It evaluates a
checked baseline and a finite list of alternatives, validates behavior before
ranking actual costs, and retains failed/regressing candidates only as evidence.
Edelweiss exercises this with its complete player movement region. Candidate
directories and artifact hashes tie costs to the C, assembler and ROM actually
measured; a local estimate or frequency hint is not presented as measured cost.
The report retains the nondominated accepted alternatives over the objective
metrics as well as the deterministic lexicographic winner. Equal costs retain
the earlier candidate. For the same input revision, a later policy can therefore
prefer speed over stack without repeating semantic validation or losing the
rejected alternatives.
Artifact hashes for the entire frontier are checked again before completion;
the report distinguishes pending, passed and failed artifact validation. This
is a finite set of measured tradeoffs, not a claim of global optimality.

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

## Closed program publication

The program image is the shared publication layer between resolution and
embedding. `SourceOptions::wholeProgram` discovers exported functions and values
through the root's parsed import closure before constructing the request. All
entries therefore share one semantic model, native specializations, interface
demand closure, activation analysis and storage budgets. Exported build values
are observed in that same solve. Selective requests use the same image format
for their smaller observation boundary.

`ProgramImage` records C unit ranges while the projector emits them. It publishes
source entries with their readers/stream interfaces, materialized native
functions, boundary validators, auxiliary helpers and shared support. Ordered
fragments reconstruct `vx_bridges.c` byte for byte; the image inventories the
ordinary, typed value, region, frame and banked products with sizes and content
fingerprints. The reader validates the image against its source plan and all
products before packaging. This is an integrity check, not a replacement for
semantic, ABI, linked-memory or stack validation.

Unit owners describe the source activations that can reach a function. They
support embedding decisions without relying on helper numbers or emitted order.
Each unit also names an optional private `support` header for the arithmetic
helpers demanded by its own calculation plan. Embedders include it with that
unit; static helpers cannot be supplied by linking the shared support from
another C translation unit. Headers and dependencies are inventoried and
fingerprinted like the code fragments. Shared support still contains global
storage and access routines; the image is not a complete target symbol linker. The existing bank planner and typed binding
reports retain their distinct physical contracts. Seeing an entire program does
not by itself choose optimal bank placement or prove a whole-program stack bound.

Edelweiss requests `gameplay/quests.vx --program --mode serial`. The root imports
sprite metadata along with gameplay. Its generator consumes the structured
`sprite_catalog` observation and the same image's unit ownership. The engine's
Konami-5 bank grouping is an explicit adapter contract; unknown or conflicting
placements fail generation. The former source command-line list, separate
metadata solves and regex-based C splitting are no longer needed. Binding
exclusions remain explicit, including the separate motion workspace that lives
across callbacks.

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

The proposed physical advisor also consumes ordinary source comments as
optimization intent. It records their provenance and interpretation alongside
candidate costs, and may choose a tradeoff or request another measurement.
Source semantics and checked requirements constrain every choice. Comments
cannot discharge ownership or equivalence obligations. Relevant comment edits
invalidate dependent advice; the current whole-file workspace cache already
invalidates on such edits and does not yet separate semantic and advice reuse.
The [RFC contract](vexel-rfc.md#comments-as-optimization-intent) defines this
boundary. Comment interpretation and advisory selection are not yet wired into
the measured backend search.

Validation checks observations, rejected programs and generated execution.
Tests for retired semantics are replaced by migration/contract tests. Independent
oracles and changes of solver order are useful; repeatedly counting assertions
or proving implementation details is not an acceptance criterion.

## Calculation intervals in ordinary C

`source_value.cpp` owns both the typed arithmetic graph and the shared-node
scheduler used by ordinary expressions, value bindings and region bindings.
`projectCalculation` accepts typed snapshot leaves and certifies a total,
straight-line integer interval. It returns a C comma expression, typed scratch
requirements and the exact runtime helpers needed. It does not execute the
source or introduce a new source-level representation.

`source_calculations.inc` embeds each interval at its original evaluation site.
The ordinary renderer still owns conditionals, readiness, effects, traps and
parallel loop transfers. Unsupported operations split intervals rather than
making their evaluation eager. Each retained calculation is assigned before
its consumers. Distinct argument expressions get distinct scratch lanes because
C leaves their evaluation order unspecified. Completed statements can reuse
lanes; no value cache survives a statement boundary.

The common scheduler follows each retained value through its last consumer.
Consumers are complete assignments or output stores, including every byte of a
wide copy. Freed slots can serve later values with exactly the same width and
signedness. Scheduling order and arithmetic are unchanged; allocation happens
before reading the new value's operands, and release happens after the store.
Finite-table dependencies follow the physical lookup index, which can have been
introduced after the original semantic nodes.

Branch factoring passes future output and guard roots into the preceding fixed
output block. These roots keep shared producers live across that boundary.
Inherited temporaries never enter a child's free pool, and new automatic slots
remain in the C lexical scope that declares them. This conservative ownership
rule avoids needing a separate global allocator or merging branch-local names.
The same schedule serves comma expressions, value bindings and region transfers;
region copy-cycle storage and boundary snapshots keep their existing lifetimes.
Logical retained-value counts remain distinct from physical scratch bytes.

`source_numeric.cpp` owns the shared native integer range transfer used by
program analysis and the calculation graph. Converted intervals widen to the
whole destination domain when they cross a modular discontinuity.
The immutable graph can publish exact intervals as constants. Before propagating
residue demands, it can choose a smaller arithmetic width when both converted
operands and the complete result fit that width with unchanged signedness.
This whole-value proof also covers division and right shifts; the shift count
must remain below the chosen width. It does not infer a smaller producer from
a small final result. Mutable region transfers keep their established physical
operation widths, while benefiting from the common interval transfer.
The same transfer also handles bounded shifts and nonzero divisor intervals,
including signed truncation, arithmetic right shifts and minimum/-1 wrapping.
Shift counts retain their 32-bit interpretation instead of being truncated to
the value width. Successful-value envelopes remain separate from completion
certificates; invalid or uncertain domains retain a conservative full range.
The scheduler chooses a smaller physical cell only when its entire result
interval fits; operations retain their logical width. Byte-copy/equality paths
check physical cell types as well as graph types. Branch-dependent facts remain
local to their established choices and cannot narrow an enclosing shared cell.

The value graph also propagates low-bit demand backward from every output.
Demands join before any node changes width, so one full-width consumer retains
a shared producer's full calculation. Modular arithmetic, casts and selection
arms can propagate a smaller demand; comparisons, truth tests, division and
right shifts require their complete operands. A valid original left shift past
the retained width becomes zero, rather than an invalid narrower shift. This
physical view is confined to closed pure graphs and ordinary calculation
intervals; mutable region transfers keep their established widths and ordering.
`narrowed_operations` reports these choices separately from range-narrowed
scratch cells. Unsigned calculation results explicitly preserve zero extension
when returning to the ordinary C renderer.

The graph can collect repeated additive terms in one modular ring before
scheduling. It traverses equal-width sums, differences, constant coefficients
and bit-preserving casts, with explicit term, traversal and remaining-graph budgets. Different-width
conversions are opaque, and collection never distributes nonconstant products.
`linear_policy`, `linear_terms` and `linear_combinations` expose this bounded
alternative for function and binding scopes. Its validity is independent of
whether SDCC's resulting instruction sequence is faster.

Typed leaves also preserve physical publication provenance. An ordinary
calculation can receive a call result or loop cell whose value is fixed for this
interval but whose producer is in the same C activation. SDCC operand barriers
must recognize those leaves and computations derived from retained cells;
treating every graph input like an incoming function parameter loses relevant
backend information. Support-header demand follows selected expressions, even
when an algebraic rewrite needs no scratch storage.

The same physical leaf interface carries the closed numeric envelope. The
read-only `InterfaceDemand::transportRange` maps compact result and loop slots
back to their logical leaves, then converts the established bounds to the
chosen carrier. Formal arguments and captures use the union of callers;
materialized returns retain their invocation-specific envelope; carried cells
use the anchored backedge envelope. Ordinary calculations therefore consume
the same facts as interface planning without starting another analysis or
substituting a producer for its snapshot. Public inputs retain their declared
range checks. Unsigned residue storage receives the wrapped interval, never
the original logical bounds. Missing facts leave the full carrier domain.

Finite realization enumerates the admitted input or computed-cut envelope.
Unreachable byte patterns receive a defined representative value. Existing
entry checks and cut producers remain executable, so this does not enlarge the
source domain or authorize invalid calls. An undefined finite candidate is
rejected locally and retains native code; it cannot turn an optional table
search into a source-resolution error.

`CCalculations` can plan an ordered group of at most 64 function results through
one graph. All expressions must be admitted as total computations over stable
snapshots. Each returned comma expression contains only the producer stores
needed before that output; shared values survive until their last output.
The caller emits these expressions in order without intervening effects. This
covers both private result structures and public payload serialization. An
unproved expression or a group without retained shared work uses the existing
independent intervals. Planning and emission consume the same cached group,
with typed scratch included before frame allocation and bank partitioning.
The `calculation_result_sharing` allowance and its physical-function scopes
expose the alternative. Reports publish `result_policy`, `result_sharing` and
`result_groups`; these are plan choices, not measured speed or stack claims.

Public payload serialization has a separate, shorter lifetime. A multi-byte
leaf is evaluated once into an unsigned carrier, by default matching the solved payload
width, then every byte is written before the next leaf is evaluated. All such
leaves in one activation therefore reuse one carrier. A 16-bit payload needs
one two-byte snapshot; a 32-bit payload needs one four-byte snapshot. Embedded
constants and byte payloads need none. The cast occurs after the original
expression has been evaluated, and the reader retains its logical signedness
and width checks. Frame planning charges exactly this reusable carrier before
selecting automatic or exclusive static storage. This does not extend its
lifetime across a callback or weaken the activation proof for static locals.
The `calculation_publication_reuse` allowance and physical-function scopes can
retain separate snapshots instead. `calculation_publication_narrow` and its
scopes can independently retain 32-bit carriers. Snapshot counts, carrier widths
and bytes appear in frame reports, allowing measured selection among four
bounded alternatives. A smaller buffer does not establish a smaller SDCC stack:
changing its width can also change register spills in the surrounding function.

After operation widths close, the pure calculation graph can pair reachable
quotient and remainder nodes with identical operands, width and signedness.
The optional `calculation_division_sharing` / `value_division_sharing` choices
rewrite the remainder to a modular subtract/product using the existing
quotient. The remainder's proved envelope survives this identity even though
independent interval multiplication loses the relation between the operands.
Parents are rebuilt in dependency order; exhausting the bounded graph budget
rolls back this optional rewrite. Mutable regions and power-of-two divisors
retain their previous realization. Reports expose `division_policy`,
`division_sharing` and `shared_divisions`. The count describes graph rewrites;
later finite realization can remove their runtime work. The default is zero:
automatic storage can trade fewer cycles for more observed stack, while fixed
storage can accept the same sharing without that stack increase.

Unsigned-byte rotation helpers are a separate opt-in realization, controlled by
`calculation_rotation_helpers` / `value_rotation_helpers` and their usual
function/binding scopes. Matching uses DAG identities and stops at materialized
shifts, so it cannot reread a producer whose scratch slot has been released.
Inline one-bit C rotation steps give SDCC compact register instructions. The
physical choice remains explicit because expansion affects the enclosing
register allocation, even after removing the earlier helper call/return cost. Reports expose
the policy identity and effective choice for measurement adapters.

Constant division has an independent unroll allowance, enabled by default.
`calculation_division_unroll` and `value_division_unroll`, with their respective
function/binding scopes, select either divisor-specific straight-line helpers
or the already proved compact width-specific loop. Power-of-two operations
remain direct. Function reports expose `division_unroll_policy`, the effective
`division_unroll`, and `unrolled_division_helpers` counted from demanded helper
identities. The MSX realization adapter uses this fact when an object exceeds
its bank or SDCC reaches a resource limit: it disables unrolling only for that
rejected owner before changing continuation groups. This closes cases where
one expression's helpers exceed a bank and statement partitioning cannot help.
Each allowance changes at most once; subsequent partition limits strictly
decrease, so retry remains bounded. Source revision identity and all original
constraints are rechecked on every solve. `realization.json` retains both
`partition_limits` and `realization_preferences` for each measured attempt.

The same scheduler can instead keep each retained value in a distinct slot.
Both alternatives preserve the evaluation schedule and last-consumer proof;
only physical storage assignment changes. Closed function/binding reports expose
the scoped `scratch_policy`, effective `scratch_reuse`, and `scratch_reuses`
count. The typed preferences are global allowances intersected with per-function
or per-binding allowances. This separates the validity of a realization from
the backend's preference among valid realizations.

The measured-search helper enumerates a bounded neighborhood of these reported
choices, including joint changes. Each evaluator resolves the complete program,
builds a separate target project and returns validated artifact hashes and
measured costs. Selection compares whole-program RAM/ROM and supplied stack/time
limits; an isolated frame-byte estimate is insufficient when overlays and SDCC
spills interact. An input-keyed cache reuses only matching compiler, source,
toolchain, platform, workload and preference measurements whose artifacts still
match. Evaluators remain responsible for defining that input identity and for
bounding subprocesses. Sample-based timing and stack observations remain
explicitly separate from static bounds and semantic validation.

Search order is also a physical choice. An optional validated priority list
orders the bounded neighborhood without changing its candidates' meaning or
acceptance checks. The game adapter uses source frequency hints to try hot
bindings earlier. An exhausted wall budget leaves unmeasured choices unknown;
it does not prove that the retained candidate is optimal. A whole-project wall
budget includes time waiting for serialized SDCC, while each running compiler
process retains its separate memory and execution limits.
The compiler child inherits the serialization-lock descriptor and a kernel CPU
backstop. Killing its supervisor cannot release that lock while the child still
runs. Normal cancellation terminates the compiler process group; queue wait,
running wall time, CPU consumption and peak RSS are recorded separately.

The storage-search adapter also separates its independent C/reference evidence
from Vexel candidate evidence. A checked reference profile can survive a Vexel
solver or helper-vector change because neither is a reference build input.
Other source, generated-directory, toolchain, platform and environment identities
remain in its key. The complete profile tree and scenario evidence are verified
on every cache hit; the directory used by the build must be that verified tree.
Fresh reference evidence is published only after successful behavioral checks
and a second input-identity check. This reuse skips compilation, while each
candidate still executes the full comparative scenario. It does not reuse a
Vexel candidate's cost under a different workload.

The complete trace and demanded outputs are planned before `FramePlanning`
chooses automatic or exclusive fixed storage. Per-type lane high-water marks
contribute to `projected_local_bytes`; the report separately exposes
`calculation_scratch_bytes`, `calculation_intervals` and `retained_calculations`.
Emission checks the closed storage/helper plan. These byte counts are not a
worst-case stack bound. Bank continuations capture the same declarations, and
bank modules include their own required arithmetic helpers. Resident helper
placement follows `VX_RESIDENT`, allowing an embedding such as Edelweiss to
supply its own mapper contract without hardcoded section pragmas.

The ordinary frame allowance currently covers the complete published image.
An embedding may omit ordinary entries that it replaces with typed bindings,
so planned static bytes can exceed the static bytes that reach its executable.
Selection must therefore use the linked allocation as its RAM cost. The next
extension here is to feed the embedding's demanded units into frame investment;
the current implementation does not infer that demand from a smaller linker
result or retroactively treat an omitted function as an activation proof.

Public payload encoding is another independent physical decision. The current
encoding policy prefers 16-bit elements when feasible. Reducing the shared arena
cap can force byte encoding for one source, but it also constrains every other
source using that arena. Edelweiss demonstrates the limit: its password encoder
contains only bytes, while mixed-width player and world results still require
20 bytes under uniform per-source element encoding. A smaller common cap is
therefore not a substitute for a measured per-source encoding preference. Such
a preference remains future work; snapshot-width selection does not change the
current public payload format.

Fixed local declarations also leave the private call interface intact: C still
transports its arguments and results, and SDCC may introduce spills. A future
choice of fixed argument/result slots would need its own activation and
evaluation proof. In particular, all argument expressions must finish before
publishing a reusable call frame, since a nested call during argument evaluation
could otherwise overwrite an outer call's prepared arguments. Local-frame
exclusivity alone does not establish that additional lifetime contract.

The current interval certification supports the same native integer widths and
total arithmetic subset as the typed value graph. It deliberately leaves table
search, cross-guard scheduling and retention across effects to other realization
policies. Extending those policies must preserve snapshot and activation proofs,
not merely recognize equal expression text.
