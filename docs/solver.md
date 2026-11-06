# Vexel as a solver

The central question is: **what information is necessary to determine this
observable result?**

An exported value, an exported invocation, or an explicitly requested result is
a root. Each root asks for facts. Facts ask for other facts. A worklist applies
rules when their dependencies become available. A rule can also discover new
obligations: a resolved call creates an invocation, a known condition selects a
branch, and a known array shape creates iteration instances.

Interpretation, specialization, and discovering build inputs are consequences
of that process. They are not separate semantic engines.

## A small puzzle

```vexel
^answer:#u32 = factorial(n) + bytes;
n = 6;
bytes = |::assets::message.txt|;
&~factorial(x) { x == 0 ? 1 : x * factorial(x - 1) }
```

Demand for `answer` asks for the invocation `factorial(6)` and the resource
length. Demand for that invocation creates its parameter stores, comparison,
conditional, and return obligation. The comparison selects the recursive arm,
which asks for `factorial(5)`. These are worklist events, not recursive calls to
a C++ interpreter. At zero, only the base arm is activated. The resulting value
facts wake their dependents.

The resource becomes a file snapshot and an entry in the build dependency set
only when it is demanded. The resulting constant can become a fixed array size,
an exported value, or a function argument through the same rules.

```mermaid
flowchart LR
    answer["demand answer"] --> call["factorial(6)"]
    answer --> length["resource length"]
    length --> file["snapshot message.txt"]
    call --> guard["6 == 0"]
    guard --> arm["activate recursive arm"]
    arm --> next["factorial(5)"]
    next --> more["... factorial(0) = 1"]
    more --> result["known return facts"]
    file --> deps["Make dependency"]
```

The graph now also supports explicit finite relations: `?{x=?(#u8,0..10);
?(x*2==6);x}` determines `x=3`. Source assignments retain their ordinary value
semantics; a finite choice is explicit syntax. The solver checks the uniqueness
of the observable result, rather than publishing its first satisfying witness.

## One source-backed graph

`Graph::nodes` is a single arena. It contains:

- source files, lexemes, trivia, and delimiter-pair links;
- structural declaration, expression, and type sites;
- scopes and local slots;
- binding, call, type, value, state, and resource obligations;
- dependency edges, reverse subscriptions, and completed facts.

The parser temporarily uses a vector of IDs to index graph-owned lexemes. Its
recursive routines recognize syntax and populate structural nodes. They do not
create an AST for another subsystem to own.

The structural builder walks source sites to create instantiated nodes and
edges. It does not return semantic answers. `Solver::rule` and
`Solver::typeRule` dispatch value and type obligations on that graph; arithmetic
and conversion helpers operate on facts already supplied to a rule, never on
source syntax.

Every site records immutable file offsets. Invocation identities incorporate
the schema site and argument types and values. Runtime arguments also include
the identity of their producer, preventing independent runtime inputs from
being confused. Numeric arena IDs are handles local to one solve; they can
differ between scheduling orders and are not persistent cache keys.

## Information states

The current implementation has an intentionally small information domain:

| State | Meaning |
| --- | --- |
| No published fact | Not yet determined; wait on dependencies |
| Fact with a constant value | Type, value, and effect grade are known |
| Fact with a runtime value | Type and runtime dependence are established |
| Fact with a partial array | Shape and some elements are known; other elements depend on runtime |
| Structured diagnostic | Distinguishes rejection, ambiguity, an unfinished proof, invalid replay and exhaustion |
| Undemanded node | No root currently asks for this obligation |

Value facts carry a type, a value and an effect grade. Independent type
obligations have their own subscriptions and can publish partial record types
before values are demanded. A field can retain an unresolved type obligation
while another field is available to a query. Type refinements preserve already
established properties within that resolution context.

Finite solve regions have a separate reversible domain state. Symbolic scalar
values identify variables in that context; arrays and records can contain such
values. Arithmetic rules generate finite relations using the same conversions,
wrapping and trap semantics as ordinary evaluation. Propagation removes
unsupported values, records the reason on a trail, and wakes dependent
relations. Search restores the trail after each assumption. A successful solve
materializes its observable value only after checking for a distinct answer.

A runtime value remains established runtime dependence, not an unresolved
choice. Budget exhaustion is distinct from impossibility. Completion properties
track normal paths and explicit exits separately from values and effects;
type-only completion avoids evaluating return payloads. General cyclic
shape/type inference and arbitrary physical-layout search remain outstanding.
Optional blocks currently isolate speculative
semantic validation by copying the graph and using the same dispatcher in the
trial revision; this is correct but deliberately not an optimized persistence
scheme.

No fact is permanently marked irrelevant. Demand can activate a previously
unused declaration later in the same solve.

## Demand and control

All files loaded as source must be lexically and structurally valid. Semantic
obligations are created only in demanded declarations and activated branches.
An invalid identifier in an unused function is harmless; an unmatched brace
in that function is still an error.

A known conditional creates only its selected branch. A runtime conditional
creates both branches, requires compatible result types, and preserves a
conditional join. Short-circuit operators use the same discipline. Runtime
branches may produce a constant result when both alternatives have the same
value, while still retaining branch effects.

Within an activated block, statements are sequenced and evaluated strictly.
The prototype does not perform effect analysis to discard unused local
initializers. Dead declarations and dead branches are the supported semantic
pruning boundaries.

## Local state without an interpreter environment

The lexical scope map answers only which slot a name denotes. It never stores
the current value of a variable. Deferred branches, iterations, and type
expressions capture their lexical bindings when created, so later declarations
cannot alter the meaning of an earlier source site.

Each local declaration has a `Slot` node. Writes create `Store` nodes linked to
the preceding state. Reads create `Load` obligations. A load follows state
edges until it reaches the appropriate store. Traversing an edge creates
another graph obligation; it does not recursively evaluate the program.

At a runtime branch, a `Merge` node records both reaching states. Reading a
slot after the join creates branch-local loads and a value merge. If both
stores hold the same value, the merged value is constant. If a branch reaches
the entry state without an initializing store, the read is rejected.

Unrolled loops create distinct iteration scopes, `_` slots, and stores, linked
in execution order. This gives local mutation its normal sequential meaning
even when the worklist solves independent facts in a different order.

Compile-time repeat expands its condition/body incrementally through the
worklist. Runtime loops instead use closed `LoopHeader`/`LoopExit` regions,
with typed entry seeds and runtime `LoopValue` facts for carried locals.
Unchanged locals retain their incoming facts. This avoids waiting for an
unfinished previous iteration. See [architectural stress tests](architecture-stress.md)
for the implemented boundary and remaining limitations.

Array element writes create `Update` values and then store them back to the
logical slot. Copies remain independent. Index paths evaluate once, and
unrelated writes in a compound assignment's RHS survive the final replacement.

## Invocations, effects, and reuse

A declaration is a schema. Calls resolve candidates from name, arity, argument
types, and representability. Return context does not select a target.

Each instantiated call creates parameter slots and an invocation return
obligation. Constant recursion grows this graph rather than the host evaluation
stack. Re-entering an identical ancestor invocation creates a backedge; a
demanded nonproductive cycle then fails at fixpoint. Growing recursion is
bounded by graph and worklist budgets.

Runtime recursive edges instead refer to a closed invocation interface. Return
type equations can be seeded by a base path. Argument generalization, capture
state generalization and effect closure use explicit revisions: the owning
checkpoint is restored before changed assumptions are applied. A nested optional
or finite revision propagates such a request to the checkpoint that owns the
invocation. No previously published value is changed in place. Materialization
preserves overlapping recursive activations and captured logical locations.

Completed invocations without external or control effects can be reused for the same
input facts. Unfinished independent calls are not merged: doing so would risk
merging external effects before their effects are known. Consequently some
branching constant recursions can expand substantially before reuse becomes
available. This is a performance limitation, not permission to change results.

Effects are tracked separately from values:

- `pure`: no state mutation or external operation;
- `local`: parameter/local stores, contained within an invocation;
- `control`: residual loops or possible runtime traps;
- `external`: an external operation is present on an activated path.

Parameter initialization currently contributes the conservative `local` grade.
Both `pure` and `local` are acceptable in constant calculations. External and
control effects forbid using the invocation as an immutable global initializer
even if its final value is known. A typed mutable global can retain such an
initializer for one-time runtime execution. External calls are never executed
by this tool.

The effect grade summarizes an obligation; it does not encode effect order.
Order is carried by state, predecessor, branch, and call-instance edges.
Sharing a numerical answer does not remove those edges.

## Build inputs and outputs

Source imports are loaded eagerly, with their declarations installed in the
importing lexical scope. Resources are read lazily and cached as binary-safe
snapshots for the duration of a solve. The accepted dependency set records source
files and observed inputs that support the result, including evidence which
justifies a fallback or refutes an alternative.

`--depfile FILE --target TARGET` projects that set into Make syntax.
`examples/build.mk` demonstrates rebuilding a JSON artifact after a resource
change. There is no separate user-written resource list.

Directory resources snapshot a sorted listing and lazily read file contents.
Opt-in process imports snapshot bounded command output, including failures,
so backtracking does not repeat the process. Their side effects cannot be
rolled back and their external input dependencies are not discovered.
See the executable profile for the opt-in and resource limits.

Each invocation of the executable starts a fresh graph. Make owns freshness
between invocations. Missing preferred imports record their existing parent
directory, including project overrides of relative or bundled modules. Empty
prerequisite rules let deletion rerun resolution. An optional fallback retains
inputs which justified its absence result, while discarded type candidates keep
their own dependency sets. General incremental invalidation inside a running
solver and persistent caches remain future work; process imports still need
explicit prerequisites for undiscovered external inputs.

The default report shows resolved roots. `--json` exposes all instantiated
obligations and residual operations, with an explicit `demanded` flag; including
undemanded instantiated nodes keeps references such as local slot IDs complete.
`--graph` emits a Graphviz view of demanded dependencies. `--trace` records fact
publication. JSON values are rendered as strings so exact integers and binary
strings are not constrained by a JSON consumer's number representation.

JSON and Graphviz remain inspection formats. The [MSX exporter](backend-msx-neo8.md)
projects the established graph into C while preserving call occurrences,
guards and state edges. Its target facts are installed before resolution; SDCC
and megalinker build the exported project, whose checker validates measured
physical placement. The C projector never infers types or selects overloads.
Runtime trap helpers preserve the solver's numeric and indexing constraints.
It does not yet feed arbitrary linker measurements back into the live worklist.

## Validation and limits

The integration suite runs accepted programs and rejected programs using FIFO,
LIFO, and deterministic randomized worklists. It compares semantic roots, not
allocation IDs, cache-hit counts, or expansion order.

Tests exercise constant and runtime calls, local stores and joins, effects,
dead regions, arbitrary-width arithmetic, overload ambiguity, recursion,
binary resources, imports, diagnostics, depfiles, and graph projections.
`make sanitize` runs the suite with address, undefined-behavior, and leak
instrumentation. LeakSanitizer requires an environment in which ptrace-based
inspection is permitted.

Defaults bound the prototype: 200,000 graph nodes, 2,000,000 rule firings,
16,384 elements per array/range/iteration, 65,536 integer bits, 16 MiB per
input file, and 256 levels of parser nesting. Node and step budgets have CLI
overrides; `--max-elements` also bounds arrays and unrolled repetition, and
`--max-bits` bounds integer values and declared integer/fixed storage widths.
These are solver resource limits, not language integer-width limits.
Exhaustion is a structured diagnostic, distinct from a rejected constraint
assignment. Structural search and optional semantic blocks propagate it; a
resource limit never proves a candidate impossible.

## Code map

| File | Responsibility |
| --- | --- |
| `include/vexel.hpp` | Graph, facts, identities, and solver interface |
| `src/graph.cpp` | Arena, file snapshots, type/value representation |
| `src/parser.cpp` | Graph-owned lexemes and structural population |
| `src/solver.cpp` | Instantiation builder, demand, scheduler, diagnostics |
| `src/rules.cpp` | Fact rules, calls, branches, state, iteration, resources |
| `src/loops.cpp` | Runtime region signatures and incremental compile-time repeat |
| `src/numeric.cpp` | Operations on resolved numeric/aggregate facts |
| `src/type_equations.cpp` | Initializer-anchored type candidates and original equation validation |
| `src/type_properties.cpp` | Independent transforming property equations, dependency discovery and witness revalidation |
| `src/type_aliases.cpp` | Opaque type-equality proofs and closed property joins |
| `src/select_types.cpp` | Independent conditional field, element and schema properties |
| `src/output.cpp` | Text, JSON, Graphviz, and Make projections |
| `src/main.cpp` | Command-line boundary |

Types and completion already have independent demand paths. Runtime loop and
recursive invocation interfaces close supported cyclic execution relationships.
Further separation of shape and effect properties, general cyclic type equations,
and target placement relationships remains work; the existing return-equation
solver is not a general theorem prover.
