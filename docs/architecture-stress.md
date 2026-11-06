# Language features as architectural tests

A working MSX ROM does not establish that the graph can express the language.
The missing features must drive architectural work before current abstractions
become assumptions that new semantics have to work around.

The first exercise implements runtime loops, compile-time repeat, mutable array
elements and logical compound assignment through the solver and MSX projection.
It exposed limitations in both state and effect modeling.

## Runtime regions do not wait for the previous iteration

Previously a local load walked backwards through completed stores and joins.
A literal backedge from a loop header to its unfinished body would prevent
either side from publishing a fact.

Runtime loops now establish a closed region:

1. Resolve incoming state and the concrete types of carried locals.
2. Introduce a `LoopHeader` with those storage identities and entry seeds.
3. Use `LoopValue` inside the region for carried values. Their types are known;
   their values depend on runtime repetition. Untouched locals retain facts.
4. Resolve one condition/body region through ordinary worklist rules.
5. Establish `LoopExit` after the region obligations resolve.

The owner records the header, body, carried slots, seeds and exit in the graph.
Runtime repetition is part of this region's semantics. Missing source facts
are never reclassified as runtime merely because a dependency cycle exists.
The graph size of `!@` does not grow with its trip count. A header's
`runtime_backedge` points to the body exit. It is deliberately separate from
`requires`, the edges that demand facts. Graphviz shows this runtime edge with
a dashed line.

This signature is conservative: a syntactic write footprint can include writes
in a branch later proved dead. Carried locals currently require concrete types
and initialization before entry. The implementation does not solve loop
invariants, partial initialization, iteration-dependent types or arbitrary
fixed points. A zero-trip condition or empty iterable can avoid activating the
body. Condition writes/effects still execute for a zero-trip condition loop.

`(condition)@body` instead expands one condition and body at a time through
the worklist. Each condition observes the preceding iteration's state. There
is no second evaluator with a mutable host environment. Node, worklist and
element/iteration budgets bound expansion; an unknown condition requires `!@`.

## A known answer does not imply normal completion

The old grades—pure, local and external—missed computations that never return
or trap without IO. This must not become a return of the literal 42:

```vexel
&^entry()->#u8 {
    (1)!@{};
    42
}
```

Nor may `1 / divisor; 42` discard a possible division-by-zero trap. The new
`control` grade retains runtime loops and possible bounds/division/shift traps.
It participates in state sequencing, call summaries, constant-initializer
checks and emission. Completed pure-result reuse excludes control effects.

This is a conservative summary, not a termination proof. A future effect model
should distinguish divergence, traps and transfers. The change already fixes
an observable error in the previous constant-result shortcut.

## Mutation operates on logical places and value snapshots

An array designation identifies a local slot and an evaluated index path.
Indices evaluate once. A compound operation captures the old element before
the RHS, then applies the replacement to the latest containing array. Unrelated
RHS writes survive:

```vexel
&example() {
    a:#u8[3] = [1,2,3];
    b = a;
    a[0] += { a[1] = 7; 5 };
    // a is [6,7,3]; b is still [1,2,3].
    a[0] + b[0]
}
```

`Update` produces a new aggregate value; `Store` associates it with a logical
local. Known elements remain known where possible. No universal source address
or reference type is introduced.

The MSX projection flattens homogeneous array shapes into C storage and emits
explicit scalar copies. Iteration captures its array before entering the loop,
so mutation of the original cannot change the visited elements. This is a
correctness-first representation: snapshot temporaries can waste stack and ROM
until lifetime and copy analysis establish safe reuse.

## What validation establishes

Language tests compare FIFO, LIFO and randomized worklists for repetition,
region signatures, constant results with control effects, copy semantics,
nested updates and short-circuit stores.

Host-C tests cover nested loops, condition effects/mutation, multidimensional
arrays, function-parameter copies, conditional arrays and iteration snapshots.
Finite Z80 workloads are compared with reference executions made by replacing
the runtime barrier with unrolled iteration in the same source. The oracle uses
Vexel rules; the independent CPU core executes the actual SDCC machine code.

The infinite-loop case must exhaust its execution budget instead of returning
42. Bounds/division traps followed by known results must still trap. These
exercise architectural assumptions, beyond accepting syntax.

## Remaining architectural tests

This priority map drove the subsequent implementation. Completion, independent
type properties, records, optional revisions and logical receiver places now
have executable tests. Persistent/backend storage, fixed point and floating
arithmetic now have source and target tests as well. General cyclic property
equations, finer-grained demand and physical realization search remain open; consult
the [acceptance ledger](implementation-work.md) for the precise current boundary.

| Feature | Architectural question |
|---|---|
| Break, continue, nested returns | Can a region describe completion alternatives and restrict later demand to continuing paths? |
| Records and tuples | Can shape, field identity, values and physical layout resolve independently? |
| Type-of | Can typing be demanded without evaluation, including expressions that would trap if executed? |
| Probes and optional blocks | Can speculative resolution succeed or fail without corrupting committed bindings? |
| Methods and captures | Can a logical place cross a call without confusing value copies, aliasing and physical addresses? |
| Mutable/external/backend globals | Can storage effects cross calls and use target-specific access protocols? |
| Runtime recursion | Can an invocation reference a signature before the body's summary completes? |
| Fixed/floating point | Are rounding and exceptional behavior explicit semantics rather than host accidents? |

### Completion needs its own representation

Blocks expose a normal continuation and explicit exit destinations. Joins exclude
exited paths from subsequent reads, and calls retain captured writes made before
a return. Loop boundaries consume break/continue; function boundaries consume
their own returns. Both forward the remaining exits. Possible exits also delay
construction of later operands, so unreachable bindings are not diagnosed while
building a graph that cannot execute. The emitter projects these relationships
using labels, rather than inventing source control semantics.

### Type demand must be separate from value demand

Independent type subscriptions let a record expose one field's type without
requiring the value or type of an unrelated field. Type-only completion queries
can inspect a return payload without reading an uninitialized local or taking
an arithmetic trap. The common binding, overload, conversion and graph rules
remain responsible for both kinds of demand.

Tests cover forward declarations, generic record annotations, uninitialized
values, and potentially trapping expressions. General cyclic type/shape
inference remains open. Loops have independent completion projections; differing
return types may still need control facts before their type can be settled.

### Speculation needs a boundary

Optional blocks resolve in isolated graph copies, committing only successful
revisions. Tests reject candidates after they have produced local writes,
resource dependencies and finite relations. Type mismatch and arithmetic traps
remain errors; supported absence failures discard the optional region. Missing,
ambiguous, invalid and unknown are distinct outcomes; an unresolved probe is
not evidence that its member does not exist.

The RFC now makes [order independence and revision](vexel-rfc.md#resolution-assumptions--order-independence)
explicit. A candidate may be explored and then rejected after later constraints
arrive. That rejection must invalidate dependent values, type specializations,
compiled regions and target evidence. The implementation uses a domain trail
for fixed relations and graph checkpoints for candidate-dependent structure.
Tests add constraints after choices, reject structural candidates, and compare
answers under different source/worklist/branch orders. Those tests accompany
independent finite-domain and Sudoku oracles.

## Remaining architectural tests

Runtime recursion now closes invocation interfaces, revising specialization
constants, capture state, return types and effects when recursive edges require
it. Type-only calls have distinct projections. If a shared return signature
cannot express a value-dependent record or array shape, the solver restores a
checkpoint and specializes using pure control facts. No previously consumed
signature is changed in place.

Fixed-point rules use arbitrary-width scaled integers, with explicit truncation
and wrapping. The MSX realization uses two-limb intermediates and banked helpers;
independent rational/integer oracles and Z80 tests check agreement. Floating
point now defines IEEE rounding and exceptional values through integer-only
source arithmetic and banked f16/f32 helpers. External storage has explicit
target access facts. Aggregate and environment ABI tests exercise logical
aliases and completion state. Mapper RAM allocation, broader bank placement
search and stack-depth proofs remain target acceptance work.

### A type question can need part of a calculation

An array dimension inside a queried function may use one argument while another
argument remains uninitialized. A separate `PropertyValue` graph projects the
reaching stores and arithmetic needed by that dimension. Inline calls have a
`PropertyCall` projection, distinct from both ordinary execution and the call's
return-type signature. Each uses the existing overload and conversion rules.
Ordinary execution still evaluates every operand and retains traps and effects.

These projections exposed two boundaries that must survive graph rewriting.
A copied record field sheds its producer's effects without replaying that
producer; its conversion can nevertheless add an invariant check. The raw
representation feeding such a check must remain distinguishable from the
checked result, or projecting the check recursively reconstructs itself.
Host and Z80 tests cover both boundaries, including external producers and
arithmetic whose wrapping changes after an implicit field conversion.

Schema identity now has an obligation separate from field layout. A local
constructor can provide a scalar field type or an argument-supplied array shape
while an invariant's lexical capture remains unread. An unknown schema carries
its graph handle; it is never represented as a known empty schema. Whole-type
conversions and joins explicitly request closure of the needed properties.
Recursive interfaces use the same requests, including declared record returns.

This is a partial structural description, not a partially executed record.
Producing the value still evaluates every constructor operand and validates the
invariants. Worklist subscriptions resolve field and schema promises in their
own input context. Optional/structural revisions copy these handles with the
graph; no separate cache can preserve a promise from a rejected revision.
Broader joins and overload questions can still demand more of a type than a
single field query needs. General cyclic property equations remain open beyond
the initializer/argument-anchored closure described below.

An unchanged automatic location also has a useful independent property. A
`PropertyLoad` can cross `StateOf` when a conservative source proof excludes a
write to that logical slot. An unexpanded graph's lack of stores is insufficient:
the proof follows callable bodies, receiver places, expression arguments and
implicit operators, and retains the dependency when a lexical extension is not
available. This closes shapes across unrelated control without inventing values
for its conditions. Writes on either arm still require the actual state join.

A nested recursive function returning a dependent record exposed another cycle:
its return annotation waited behind `Finish`, while finishing required the
recursive result's interface. A backedge now demands that annotation directly
in its owning activation. Together with the unchanged-location proof, a frozen
schema constant remains available across the recursive body. The same example
is checked under worklist permutations, host C and actual SDCC/Z80 execution.

Implicit calls need the same closure analysis as named calls. An overloaded
operator in an invariant can capture a schema parameter, and an operator in a
recursive body can change captured storage. Missing the former merges different
schemas; missing the latter can make generated C return a stale constant.
Capture discovery now probes operator dispatch and broadcast element types.
Recursive capture generalization shares the property-read write proof. Tests
compare scalar, broadcast, logical and iteration-method recursion against source
evaluation and run the resulting cartridge on the Z80 model.

Array literals now carry an independent element-interface join beside their
known length. A shape helper can use that length while the element interface
remains undemanded. This also exposed consumers that previously assumed every
array description was complete: overload probes explicitly close the element
types they compare, and empty broadcast values close their prototype's type
without running a sample operation. Identity type conversions preserve promises;
ordinary value conversion still discharges them. A source proof that a function
has no return transfer now permits its result type to bypass earlier statement
interfaces. A local array can therefore supply shape without resolving an
unrelated element call. Required state still reaches dimension expressions, and
ordinary value demand retains every statement and effect. Functions with
possible transfers retain their completion obligations; this is a bounded
proof, not a general completion-property solver.

Generic invariant dispatch must retain an instance context at every annotation
boundary. Constructor fields, parameter arguments, initialized storage, casts
and returned values now supply that context. Parameter descriptors are cached
per call, declaration and lexical scope, and the cache is restored with graph
checkpoints. Nominally unrelated overloads can be eliminated before their schema
captures are demanded. Nested fields and array elements retain their independent
interfaces; an empty array uses its element type without inventing a sample.

Early returns require a distinct interface projection and resumed lexical
state. Reading a raw exit as if it were ordinary reaching state produced an
internal error; requiring its runtime condition merely because two type
promises differed also discarded available information. Return interface
properties now settle before that condition is requested. Recursive generic
results can use a base interface as a checked candidate. The ordinary return
annotation still validates captured identity and every demanded returning path;
an incompatible recursive layout remains an error.

## Checked cyclic interface candidates

A cyclic type query is an equation, not permission to read a provisional fact
as final. The scalar/array global implementation seeds candidates from demanded
initializer interfaces and evaluates each original annotation with the same
candidate vector. Those evaluations use the ordinary graph rules, including
source arithmetic and overload selection. Each result contributes a common-type
requirement. A stable vector is accepted only when its original equations equal
it exactly; the final graph retains separate annotation validation nodes.

Candidate values matter even for a type question. In
`a:#[{data:#u8[1/(b+1)];data}]=(#u8[0])[];`
`b:#[a[0]+(#u16)0]=(#u8)255;`, the provisional eight-bit `b+1` wraps to zero.
The other equation establishes a sixteen-bit requirement, after which the same
source dimension evaluates to zero without a division trap. A temporary error
must therefore wait while other equations can revise the vector. Conversely,
using a revised type for an ordinary value before rebuilding all consumers can
silently change arithmetic. Both type and value demands install the same vector.

An optional block exposed the checkpoint boundary: another root could already
have observed an interface before the optional trial began. Revising inside
that trial left its inherited graph prefix stale, and LIFO disagreed with FIFO.
Such requests now propagate to the enclosing checkpoint that predates those
observations. Graph restoration rebuilds dependent facts, calls and dimensions;
optional success commits the entire solver context. Temporary resource reads
remain in the shared input snapshot but do not become final dependencies unless
the accepted equations actually need them.

This implementation deliberately crosses revisions only with settled scalar
and array interfaces. Record types retain graph-local schema environments and
need rebinding before the same mechanism can safely carry them. General
nonmonotone type search is distinct from the delivered common-type closure.
Neither limitation justifies inventing a value or hiding budget exhaustion.

Scoped globals and record fields use contextual candidates without transporting
a whole schema across an owning-checkpoint reset. Their blocked graph already contains the
binding environment and argument projections. Private proof graphs retain that
context, replace still-open interfaces with candidates, and rebuild subscriptions
for the equation being checked. This prevents a provisional field type from
waking unrelated value roots. Only after validation are the candidates exposed
through the live pending interfaces. Final checks remain attached to each owner,
so separate constructors or module instances cannot share a declaration-wide
guess. Actual values still validate dependent dimensions, including runtime
trap 9 in the MSX projection.

Record-valued candidates now use this contextual strategy as well, including
top-level globals, nested records, tuples and arrays of records. Common-type
joins retain the candidate's original schema environments recursively. A proof
may create its own record descriptors, but those handles cannot become the
accepted candidate's origins. Different frozen schema parameters remain a real
conflict. Original field queries are rebuilt from their lexical descriptors;
reusing an already expanded query can leave it wired to superseded promises.

The extension exposed two further shortcuts that violated this boundary.
Generic specialization must distinguish a free type hole from a pending field
requirement. Recursive return inference must inspect converted constructor
proxies instead of raw operands. Otherwise either shortcut can publish a narrow
type before the cyclic field requirements widen it. Tests also keep fixed
annotation fields authoritative and verify that reading a known field of a
global or constructor does not require unused siblings. General unresolved
schema equations remain outside this delivered closure.

This extension exposed a scheduler boundary. A type equation may discover a
structural choice after the ordinary work queue drains. The closure controller
must route that request to its owning finite-search checkpoint, just like a
request discovered during a rule firing. Guarded invalid equations require the
same treatment. Tests distinguish a unique result, distinct observable answers,
rejected guarded branches and exhaustion under both worklist and branch orders.

## Conditional property joins

A conditional can establish an outer record or array structure before it can
establish every property of that structure. Previously, comparing its complete
branch types forced the condition even when a consumer asked only for a field
whose type was equal on both branches. This also pulled unrelated field and
schema dependencies into otherwise answerable questions.

Conditional type joins now retain separate field, array-element and schema
obligations. A field projection recursively compares only its own branch
properties. Equal outer array lengths are immediately available. If the queried
properties differ, the original condition remains authoritative; an uninitialized
condition is not guessed, and finite choices use the existing structural
checkpoint. Schema closure carries the selected origin as well as its frozen
identity, so a later update checks invariants in the correct environment.

This decomposition is allowed only when both branches complete normally without
exits. Value execution still follows the source condition and effects, while
whole-interface queries close the remaining obligations. Tests distinguish a
query that must omit an external condition from actual evaluation that must
execute it exactly once, in host C and on the Z80. The change refines type demand;
it does not claim a general independent effect/completion solver.

## Checked dimension equations and array property joins

Explicit global dimensions exposed a gap between constructor fields and globals:
`a:#u8[|b|]=[1,2]; b:#u8[|a|]=[3,4];` had two supplied shapes but no way to use
them to close its dimension queries. The candidate mechanism now constructs a
separate initializer-shape witness, retains the declared element requirements,
and checks the original dimensions independently. It never replaces an original
dimension with an unchecked initializer length. Generic record elements use
application-specific requirements in the validation graph; captured schema
origins remain in the owning context.

Nested arrays exposed another unnecessary dependency. Joining their whole element
types demanded leaf information before publishing an inner length. Common array
joins now create independent field, element and schema joins, following the same
property-demand model as conditional joins. A pending supplied element type also
suspends generic specialization rather than causing a premature mismatch.

The tests cover source and worklist permutations, nested shapes with unavailable
leaves, generic records and invariants, separate module instances, optional
rollback, finite dimension queries, and repeated root demands through the C++
API. Host C and Z80 execution exercise one-time persistent initialization after
shape closure. Type-only queries execute no external initializer; actual reads
initialize the storage exactly once.

Direct alias groups now reuse property joins, so a cyclic `#[name]` component
can expose one common field without closing its other fields or schemas. Its
original alias equations justify this decomposition: the join includes all
supplied interfaces. Type-only alias descriptors use type subscriptions, and
installing an alias no longer schedules a complete descriptor unconditionally.

That shortcut cannot apply to every partially known global. In
`a:#[b[0]]=(#u8)255; b:#[[a]]=[(#u16)2];`, both initializers must contribute to
the common-type closure. Skipping the second witness incorrectly keeps eight-bit
arithmetic and produces zero for `a+1`. The implementation therefore limits the
early property result to proved alias groups; general equations retain their
candidate discovery and validation. Tests preserve the required result, 256,
and check that a later whole-type query still demands previously unused schema
or field obligations.


## Proving equalities before selecting type candidates

A query through `#Pair(a:#[forward(b)],b:#[forward(a)]);` previously needed
settled record candidates even when `forward` simply returned its argument.
Closing those candidates read captured schema values unrelated to the requested
field. Accepting the partial initializer as a fact would hide the problem by
omitting obligations; guessing an empty schema would change the language.

The resolver now has a distinct equality proof. Each unresolved owner in a
needed declaration group becomes a unique opaque type promise in a private
copy of the context. The original annotation runs through the ordinary type
rules. Propagation is separated from candidate inference so the proof cannot
supply a guessed width, record shape or schema to make itself succeed. Only an
unchanged opaque promise, with no demand for an opaque value, establishes an
equality. Inconclusive queries retain the existing contextual solver path.

Closed equality components become property joins over all their initializer
witnesses, including currently undemanded members. Known widths then arise from
those witnesses under source common-type rules. Field and schema obligations
remain separate, with their original environments. The proof copies mutable
puzzles, counts its propagation work, and exports only owning-graph identities.
Its `type_aliases` report evidence follows checkpoint commit and rollback.

Regression coverage includes forwarding through nested calls, tuple projections
and expression parameters; three-member cycles with an incoming fourth witness;
scoped imports; unavailable captures and fields; frozen-schema conflicts;
misleading generic/typed overloads and member probes; optional and finite
contexts; and later whole-type demands on the same C++ solver. Host C and Z80
checks require type queries to omit external initialization and subsequent value
reads to initialize storage exactly once.

Reconstructing a record with `#Box(b.x+1,b.data)` expresses field equations
rather than an opaque type equality. The subsequent property-equation mechanism
checks selected paths under a joint vector while preserving the schema. Its
tests cover nested fields, arrays and tuples, cross-field cycles, overloads that
reveal additional equations, initializer witnesses that widen during revision,
finite/optional rollback, and later whole-record demands. An exact-width oracle
uses overflow to distinguish inferred widths. Host C and Z80 checks preserve
exactly-once initialization. General schema-model search remains separate work.


A strictness-2 check exposed a further negative-information dependency. Testing
whether an argument contains any exact, unconstrained literal type must inspect
pending field properties before answering no. Treating an opaque type as an
empty list of such literals falsely proved a helper equality and bypassed its
call-boundary restriction. The ordinary dispatch rule now suspends that check
on pending properties. Strictness-2 examples exercise rejection of exact tuple
fields and successful inference with concrete fields through the candidate
solver; equality proofs do not waive that precondition.
