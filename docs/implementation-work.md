# Joint solver implementation work

This is a working acceptance ledger for the complete language and joint solver,
not a declaration that the features below already work. The RFC remains the
language design, and the executable profile describes delivered behavior.

The independent [relational core experiment](../experimental/relational/README.md)
tests a replacement for the inference mechanisms of that executable. Its
acceptance results are recorded separately below; they do not mean that the
Vexel frontend already uses the new core.

## Required architectural behavior

- Pure expressions contribute relations, including relationships whose inputs
  are not yet determined. Source arithmetic remains authoritative.
- Domains, type/shape knowledge, values, effects and completion are distinct.
- Propagation can run in different orders without changing meaning.
- Search has explicit assumptions, a reversible decision trail and scoped
  conflicts. Rejected candidates cannot leak derived values or artifacts.
- Finite searches distinguish no solution, multiple observable answers, a
  determined answer, an unfinished proof, and exhausted budgets. An unused internal choice need
  not make the observable answer ambiguous.
- Source constraints and target/representation constraints participate in the
  same search model. Measured realization costs retain their context.
- Optional external proposals can reorder supported choices. Invalid, stale
  or absent proposals leave ordinary search available. Record/replay does not
  depend on calling a model again.
- Compiled pure evaluators are interchangeable with ordinary evaluation only
  under the same numeric rules and input context.

## Language coverage to complete

| Family | Current delivery | Remaining acceptance |
|---|---|---|
| Integers, arrays, local state | Arbitrary-width integers, copies, symbolic indexing, broadcasting, portable byte and Boolean casts | Broader interaction coverage |
| Finite constraint solving | Sudoku, backtracking, observable uniqueness, exhaustion and schedule permutations | Larger-scale propagators and structural-search efficiency |
| Independent type demand | Queries, partial fields/globals, conditional/array/alias property joins, helper-mediated equality proofs, checked dimension cycles, structural revisions, loop-completion proofs, and checked scalar/aggregate type equations for globals and fields | General cyclic/value-dependent inference |
| Records and tuples | Nominal/generic fields, dependent dimensions with finite/runtime constraints, updates, tuple assignment/casts, named-record ABI, checked aggregate field cycles with frozen schemas | General schema equations |
| Functions and methods | Captures, expression parameters, receivers, logical/dotted operators, overloaded stable sorting, iteration methods, recursive interfaces and revisions | Remaining interface corner cases |
| Control completion | Break, continue and nested returns across branches, all loop forms, and custom iteration bodies | Broader interaction coverage |
| Probes and optional blocks | Non-evaluating probes, transactional rollback and structured absence diagnostics | Finer-grained revisions |
| Numeric families | Integers, fixed point, IEEE f16/f32/f64 source arithmetic, broadcasting, representation casts, bundled bits/math, runtime f16/f32 on MSX | Broader target formats are backend work |
| Imports and bindings | Lexically scoped imports, forward constants, local overload sets, resources, metadata, strictness and persistent/external/backend storage | Broader persistent-state interaction coverage |
| MSX realization | Scalar/record ABI, binary external strings, internal aggregates, capture environments, recursion, comparisons, constants, editable projects, measured candidate policy | Mapper RAM allocation, general physical search and stack proofs |
| Heuristic provider | Validated numeric and finite structural proposals, fallback, budgets across revisions and offline replay of rejected attempts | Checked transformation proposals and broader representation proposals |

## Schema-equation acceptance cases

The following type-only query now establishes `#u8` for `x` without reading
`unknown`. Its helper-mediated equalities are proved with opaque type variables,
then resolved by independent property joins:

```vexel
^answer:#u8 = {
    unknown:#u8;
    #Box(x:#u8,data:#u8[x+unknown]);
    #Pair(a:#[copy(b)],b:#[copy(a)]);
    &copy(value){value}
    v:#[#Pair(#Box(2,[1,2,3]),#Box(2,[1,2,3])).a.x]=7;
    v
};
```

A later whole-record demand still resolves the original captured constraint and
reports the uninitialized read. The helper is evaluated as an ordinary function;
its name is irrelevant to the equality proof.

Transforming cyclic equations now close demanded scalar properties independently:

```vexel
^answer:#u16 = {
    unknown:#u8;
    #Box(x,data:#u8[x+unknown]);
    #Pair(a:#[#Box(b.x+1,b.data)],b:#[#Box(a.x+1,a.data)]);
    v:#[#Pair(#Box((#u8)2,[1,2,3]),#Box((#u16)2,[1,2,3])).a.x]=300;
    v
};
```

This establishes the requested `x:#u16` while retaining the captured schema.
Independent original-query checks prove each selected property under a shared
candidate vector. Initializer witnesses are rechecked on every revision;
overload selection can expose another property whose witness must join the
same proof. Whole-type and value demands still close the original schema.
`tests/test_property_equations.py` exercises this boundary and its interaction
with optional/finite revisions, later API demands, and host/Z80 emission.

General cyclic schema inference and nonmonotone type-model search remain
separate acceptance work. A proved leaf property is not a proof of its entire
record, and the common-type iteration is not an exhaustive type-model search.

## Verification strategy

Use small independent finite-domain oracles to check propagation and search;
verify Sudoku rows, columns, boxes and clues independently of the implementation.
Compare successful results across worklist and branch orders. Check restoration
after failed branches and after changed constraints. Test budget exhaustion
separately from impossibility. Preserve existing host-C, SDCC, Z80 and sanitizer
checks as language and graph mechanisms change. Record unsupported cases
honestly until their acceptance tests pass.

The source notation for explicit finite solving must remain keywordless and
distinct from ordinary uninitialized storage and runtime inputs. It will be
specified alongside the executable examples rather than inferred from missing
values or from source discovery order.

## Current work, 2026-09-29

- Added finite relation tables, propagation, reversible domains, conflict traces,
  observable-result uniqueness and separate exhaustion/unsatisfiable outcomes.
  Source expressions create the relations using the existing arithmetic rules.
- Added keywordless explicit solving and choices; Sudoku is expressed entirely
  in Vexel arrays, iterations and predicates. Independent tests check every row,
  column, box and clue, as well as source/branch scheduling variants.
- Added type demands/subscriptions independent of value demands. Type queries
  avoid arithmetic traps and external value evaluation. Record fields can carry
  independent unresolved type obligations and refine as information arrives.
- Added nominal records, tuples, generic field access, nested updates, field
  probes and isolated optional semantic blocks. Rollback tests cover state,
  resources and constraint relationships.
- Added symbolic array indexing/updates and record-valued conditional choices;
  bounds are guarded constraints, and static errors are not mistaken for failed
  numeric assignments.
- Added a bounded external branch provider, schema/domain validation, deterministic
  fallback and exact-request offline replay. Tests include invalid/stale output,
  process failure, timeout and changed source constraints.
- Added broadcast graph expansion, nested lexical captures, fresh hygienic
  expression arguments, receiver aliases and non-evaluating method probes.
- Added operator and custom iteration dispatch, with builtin fallback.
- Added local tuple assignment with one RHS evaluation and existing assignment
  conversions, aliases and runtime loop storage relationships.
- Added explicit completion properties: nested returns, break/continue,
  continuation state joins, and corresponding C projection. Type-only return
  payloads remain independent of their values; exit-bearing loop type queries
  still require region resolution.
- Added deferred construction of operands and arguments after possible exits;
  tests include unreachable invalid bindings and forwarded expression exits.
- Still outstanding: the remaining language families in the table above,
  further interface coverage and broader target planning.
  Passing Sudoku is one acceptance result, not completion of this ledger.

- Added native C++ relation filtering as an optional solver accelerator, with
  inspectable Makefile projects, fallback, and per-call differential verification.

- Added structural checkpoints and candidate revisions for finite choices that
  determine types, dimensions, ranges, repeat bounds and sorting. Input snapshots
  are shared immutably; graph facts and queues are isolated per revision.
- Added measured MSX candidate feedback into an ordinary Vexel planning puzzle.
  Different workload stack limits select different editable projects; stale
  measurement files are rejected. This remains a bounded two-candidate experiment.

## Verification snapshot

The September 30 continuation adds recursive invocation interfaces, return-type
equations, parameter/capture generalization with owning-checkpoint rollback,
and recursive effect closure. Nested and mutually recursive materialized calls
share explicit logical capture locations; recursive activations retain automatic
storage. MSX C now supports materialized aggregate inputs/results, named-record
export signatures, receiver offsets, deep comparisons and exported constant
data symbols. Portable byte and Boolean representation casts also participate
in finite relations, including aggregate-valued relation results.

The completed September 30 baseline also includes directory/process resources,
metadata, qualified calls, strictness controls and external aggregate/string
interfaces. `make test` and `make sanitize` each pass 169 tests, including host-C checks,
actual SDCC/megalinker builds and execution with the independent Z80 core.
The sanitizer run covers AddressSanitizer, UndefinedBehaviorSanitizer and leak
checking. The source Sudoku is independently checked against rows, columns,
boxes and clues. Its resolved result also passes through the NEO-8 backend;
the linked cartridge returns the expected checksum, 405. Sudoku resolution
happens on the host, before the resulting C is compiled for the cartridge.

Signature projections, shape-dependent signature revisions and fixed point
subsequently pass the complete 183-test normal and sanitizer suites, including
banked fixed-point arithmetic on the Z80. Source floating-point integration
passes the complete 191-test normal suite. Three additional floating backend
groups pass host differential and real SDCC/Z80 execution checks. Scoped modules,
bundled bit helpers and structured diagnostics pass the complete 202-test suite.
Persistent storage, one-time runtime initialization and MSX address/port bindings
pass the complete 207-test normal and sanitizer suites. Scoped external record
interfaces, recursive persistent receivers, overloaded sorting and independent
loop-completion type proofs subsequently pass the complete 220-test normal suite.

Logical-method dispatch, scalar methods lifted by broadcasting, dependent record
fields and their runtime guards pass the complete 239-test normal and sanitizer
suites. Custom iteration exits, cyclic field aliases and further independent
type projections pass 249 normal tests. Bundled math and structured numeric traps
then pass the complete 259-test normal suite, including exhaustive binary16 root
and rounding checks, independent numeric oracles, and actual SDCC/Z80 execution.
Frozen lexical schema parameters and owned runtime invariant fields subsequently
pass the complete 262-test normal and sanitizer suites. Schema captures cannot
silently retain runtime storage after its activation or replay an external
producer when a record is updated.

Direct global alias components, exact external invocation interfaces, implicit
aggregate invariant checks, invariant purity, overload-aware schema captures,
inline value projections for type questions, and structured search diagnostics
subsequently pass the complete 275-test normal and sanitizer suites. Private
invariant receiver copies and expression-parameter projections additionally
pass the complete 277-test normal suite. Independent field/schema obligations
then pass the complete 279-test normal and sanitizer suites. The bit-width budget is
configurable with `--max-bits`. Exhaustion is not evidence that a candidate is
invalid, and diagnostic wording does not decide whether search backtracks.

Unchanged-location proofs, declared recursive return equations, and preservation
of materialized parameter bindings subsequently pass the complete 287-test
normal and sanitizer suites. Implicit operator captures and recursive writes
then pass the complete 295-test normal and sanitizer suites, including dedicated
host/source/Z80 checks. Independent array length and element-interface obligations
then pass the complete 299-test normal and sanitizer suites. Constructor-specific
generic schema capture selection subsequently passes the complete 304-test normal
and sanitizer suites. Application-specific generic parameter requirements,
nested annotation context, and generic array conversions then pass the complete
312-test normal and sanitizer suites. Open requirements at initialization, cast
and return boundaries, including checked recursive result candidates, then pass
the complete 320-test normal and sanitizer suites. Independent normal-function
interfaces subsequently pass the complete 324-test normal and sanitizer suites.
Checked cyclic scalar/array interfaces pass the complete 335-test normal and
sanitizer suites. A subsequent independent late-root demand regression passes
the complete 336-test normal and sanitizer suites. Scoped globals and cyclic
scalar/array record fields then pass the complete 348-test normal and sanitizer
suites, including late structural choices, guarded rejection, record return
interfaces and runtime invariant traps on the Z80.

Record-valued equations, including nested records, tuples, arrays of records,
fixed annotation fields and frozen captured schemas, subsequently pass the
complete 362-test normal and sanitizer suites. Recursive return inference uses
converted field requirements, and partial global queries leave unused siblings
undemanded. New MSX checks validate recursive materialization and captured
invariant traps after record interface closure.

Independent conditional field, element and schema properties subsequently pass
the complete 372-test normal and sanitizer suites. Queries can use equal branch
properties without evaluating the condition or unrelated fields; differing
properties still require the condition. Host and Z80 checks verify both omitted
external calls in type-only queries and exactly-once effects in value execution.

Validated numeric and finite structural heuristic advice subsequently passes the
complete 381-test normal and sanitizer suites. Advice can reorder alternatives
without pruning them; request budgets and replay evidence include rejected
revisions. Source, known resource and target context changes invalidate replay.

Checked explicit array-dimension equations and independent common array
properties subsequently pass the complete 397-test normal and sanitizer suites.
The checks include nested shapes, generic record elements, original dimension
conflicts, finite queries, optional rollback, later C++ API root demands, and
one-time persistent initialization in host C and on the Z80. An independent
oracle additionally checks 480 length/value-dimension cases across declaration
and worklist orders.

Independent properties in direct type-alias components subsequently pass the
complete 404-test normal and sanitizer suites, with no skipped tests. Record
fields, global aliases, nested array shapes and tuple fields can resolve without
unused sibling or schema properties. Late whole-type queries still close those
obligations. Generic equation witnesses retain their widening rules; host and
Z80 tests preserve exactly-once persistent initialization. Sanitizers report no
address, undefined-behavior or leak findings. The final logs are retained as
`build/rfc-alias-properties-404-regression.log` and
`build/rfc-alias-properties-404-sanitize.log`.

Helper-mediated type equality proofs subsequently pass the complete **415-test
normal and sanitizer suites**, with no skipped tests. The original open-schema
acceptance example now resolves through ordinary forwarding functions. Tests
retain delayed field/schema demands, all component witnesses, scoped instances,
frozen captures, strictness-2 preconditions, optional/finite rollback, and later
whole-type demands through the C++ API. Host C and Z80 execution preserve
exactly-once persistent initialization. A separate arithmetic oracle verifies
486 common-width cases by overflow behavior across declaration orders, helper
forms and worklist schedules. Sanitizers report no address, undefined-behavior
or leak findings. Final logs are `build/rfc-semantic-aliases-415-regression.log`,
`build/rfc-semantic-aliases-415-sanitize.log`, and
`build/rfc-semantic-aliases-oracle.log`. The transforming record case above
is addressed by the subsequent property-equation work; its full verification
is recorded separately when complete.

These results validate the delivered profile. They do not mark the remaining
RFC and target acceptance work above as complete.

Transforming property equations pass the complete **426-test normal and
sanitizer suites**, with no skips or sanitizer findings. Eleven new groups
cover unresolved captured schemas, nested paths, cross-field widths,
overload-discovered dependencies, witnesses revised from 32 to 64 bits, finite
and optional rollback, later API demands, and host/Z80 initialization.
Tuple fields now retain independent promises. Reports distinguish verified
`type_properties` from whole-interface and opaque-equality proofs. Logs are
`build/rfc-property-equations-426-regression.log` and
`build/rfc-property-equations-426-sanitize.log`.

Closure outcomes and build freshness pass the complete **440-test normal and
sanitizer suites**, across 46 groups, with no skips or sanitizer findings.
An unfinished structural alternative cannot prove uniqueness or impossibility;
established relations can still refute a context with an unfinished result.
Structured search reports distinguish unresolved obligations from exhaustion,
ambiguity and rejection. Optional fallback proofs retain their input premises;
new preferred imports and deleted inputs correctly trigger Make resolution.
Dependencies used only by abandoned type candidates remain isolated.

The standalone `tests/check_closure_oracle.py` additionally passes **1,296
independent cases**, covering every three-alternative mapping to two completed
answers or an unfinished proof, all allowed subsets, three worklist schedules
and two branch directions. It derives expected outcomes from set membership.
Final logs are `build/rfc-closure-440-regression.log`,
`build/rfc-closure-440-sanitize.log`, and
`build/rfc-closure-outcomes-oracle.log`.

The retained `build/closure-sudoku` project connects host solving with compiled,
differentially checked relation filters to SDCC/megalinker and Z80 execution.
Its cartridge returns the expected solved-grid checksum, **405**, under the
expanded-slot model with interrupts. The source search uses 1,083 finite
variables and 2,004 relations. Placement reports 1,743 resident bytes; this
workload observes 24 stack bytes. Neither measurement is a general stack proof,
and Sudoku search runs on the host. The
[closure contract](closure-contract.md) consolidates these guarantees, the
revision model, input freshness, target evidence and remaining inference limits.

## Independent relational core experiment

`experimental/relational` implements structural terms, occurs-checked
unification, fresh generic relation instances, reversible clause alternatives
and arbitrary-precision integer constraints. A fraction-free linear solver
closes mutually dependent dimensions without initializer seeds or finite
enumeration. Source observations and physical realizations are separate: an
explicit optimization policy may select a realization but cannot settle
disagreeing source observations. Unfinished alternatives remain obligations.

The activation adapter describes events, ordering and storage lifetimes,
including payloads retained after a producer returns. Closed event models admit
all orders allowed by their edges. Proven overlap contributes disjoint-storage
constraints; unknown overlap in an open model remains unresolved. The kernel
has no special thread, interrupt, type-widening or reentrancy mechanism.

Final normal and address/undefined-behavior/leak sanitizer runs pass **21,767
C++ assertions** and all three Python test groups, with no sanitizer findings.
Independent oracles include **5,952 integer linear systems**, **1,296 closure
outcome combinations** and **216 joint type/layout configurations** across
resolution and branch orders. A counterexample caught an integer-domain loss
when simplifying `0*x=0`; the corrected implementation preserves the numeric
obligation through both simplification and variable aliases. Logs are
`build/relational-test.log` and `build/relational-sanitize.log`.
An explicit completion/start handoff also permits storage reuse; a regression
checks both lifetime argument orders and the resulting shared plan.

`make relational-msx` uses the new machine's selected plans in three editable
SDCC/megalinker projects. Each passes **753 host-C input pairs and eight Z80
input pairs**. With six payload bytes available, serial lifetimes select shared
`Array(UInt(16),3)` buffers. Nested overlap and retained output select separate
`Array(UInt(8),3)` buffers. Deliberately forcing aliasing produces independently
checked wrong results in the latter two cases. The final binary hashes, selected
plans and execution evidence are retained in
`build/relational-msx/run-c3k4nknv/comparison.json`; the run log is
`build/relational-msx.log`. The budget concerns payload storage, not the complete
resident or stack footprint.

That first experiment supplied relations and finite activation graphs through
C++. The following source slice extends it. Automatic internal lifetime
extraction, recursive stream bounds, general nonlinear solving, persistent
incremental search and mapper-RAM/stack proofs remain open. Existing frontend
tests remain reference evidence; their internal proof structures are not
requirements for this replacement.

## Relational source-to-execution slice

`vexel-source` now adapts real Vexel source into the relational machine. It
reuses the parser and source arena but links no `vx::Solver` code. Source
property promises contribute equations for types, values, array lengths,
elements and record schemas. The adapter performs no separate numeric
evaluation or widening iteration. Generic nonrecursive calls instantiate
lexical environments; record annotations retain nominal identity and the
captured type, length or value properties they actually read.

`examples/relational_source.vx` resolves mutually dependent lengths 4 and 3
without initializer seeds, uses those lengths in generic record constructors,
and answers a partial field query while an unrelated complete schema remains
open. Later requests rebuild obligations from the same source snapshot. A
serial target plan can therefore be replaced by an overlapping one without
changing the logical `#u16[3]` result type or relying on previous choices.

The physical model derives result leaves from source structures and combines
their ranges, byte extents and lifetime interference with source constraints
in one search. Known result fields are embedded as constants; they neither
consume payload RAM nor force wider storage for independent runtime leaves.
Boundary range annotations are enforced by generated C checks. Closed models
produce inspectable C without the established frontend or evaluator.

Final normal and address/undefined-behavior/leak sanitizer runs pass **21,881
C++ assertions**, the three joint-model Python test groups and **11 source
test groups**, with no sanitizer findings. These include partial and
later complete demands, captured type/length/value separation, generic and
nominal records, seedless dimensions, unsigned arithmetic, implicit conversion
and indexing rejection, retained source types across physical choices, open
contracts, exhaustion, unsupported-call closure and emitted boundary checks.
Host C additionally agrees with an independent wrapping-arithmetic oracle for
all 256 unsigned-byte inputs. Logs are `build/relational-source-test.log` and
`build/relational-sanitize.log`.

An additional **546-case source oracle** covers 18 permutations of complete
declarations/worklists/branches and 528 linear-shape cases derived independently
from integer equations. It is retained as
`experimental/relational/tests/check_source_oracle.py`; its log is
`build/relational-source-order-oracle.log`.

Source arithmetic checks exact operand representability before concrete
operations: `(#u8)1+300` is rejected, while `(#u8)255+1` wraps to zero. The
counterexample is retained in `build/relational-operand-counterexample.log`,
and the corrected cases are covered by the normal and sanitizer source tests.

`make relational-source-msx` independently generates three editable
SDCC/megalinker projects. Each passes **753 host input pairs and eight Z80
pairs**. Serial outputs use shared 16-bit slots; overlapping or retained outputs
use separate 8-bit slots. The logical data type remains `#u16[3]`. Deliberate
aliasing produces a concrete wrong result. Final source, executable and artifact
hashes are retained in `build/relational-source-msx/run-q5d_sdr9/comparison.json`;
the run log is `build/relational-source-msx.log`. The projects occupy **4,463
resident bytes** for serial and **4,054 bytes** for overlapping/retained output,
including their test harnesses. Six bytes are result payload; the stack reserve
is still a budget, not a maximum-usage proof.

This is a bounded source slice, not a replacement for the full main frontend.
The remaining language and target limits, exact commands, output ABI and
revision contract are documented in
[the source adapter profile](../experimental/relational/SOURCE.md).

## Delayed construction, recursive properties and ordered state

The relational source adapter now exercises scalar overloads, recursive
specialization, recursive unsigned type equations, scalar local updates and
expression conditionals. Two generic kernel operations make this possible:
shared obligation bundles, activated once in each search context, and delayed
builders, which receive declared ground inputs and construct further goals.
Cached construction never shares a candidate's bindings. Structural and
primitive propagation precedes copying contexts for clause search.

Applicability and specificity use ordinary source clauses. Selection waits for
all candidate scores, and only the unique winner's body becomes a dependency.
Equal best scores report ambiguity even with equal body results. A destination
annotation cannot break the tie, and a failing preferred body cannot enable a
fallback. The matching profile currently covers unsigned/Boolean scalar
annotations, generic parameters, arity and exact-literal representability.

Known scalar arguments specialize `&~` calls, including direct and mutual
recursion. Conditions construct only a known selected branch. Type invocation
properties are separate from value invocation properties: anchored recursive
signatures can close without proving value completion. Revisited unfinished
value invocations remain unresolved; growing construction obeys work/arena
budgets and reports exhaustion. Multiplication by zero does not erase an
unfinished call. Runtime recursive value projection is still missing.

Local locations, stores and reaching versions preserve reads before later
writes. Updates convert to the location type; parameters have activation-local
storage. Nested read-only captures see their lexical location's reaching
version. Cross-boundary writes remain open pending an explicit call-state
interface. An expression conditional on a store's right-hand side can provide
a runtime merge. Untaken constant branches do not demand unsupported code.

The review also made delayed schema capture closure explicit. A known dimension
does not certify that an overloaded/conditional/recursive invariant's entire
capture support has been discovered. Such whole-schema demands stay unresolved
in this adapter, rather than publishing an incomplete capture identity.

Normal verification passes **21,941 C++ assertions**, three joint-model test
groups and **16 source test groups**. The standalone source oracle still passes
**546 cases**. The new combined control example additionally passes **18
declaration/worklist/branch permutations**, with **4,608 generated-host-C input
checks** against an independent arithmetic formula. The permutation script and
evidence are `experimental/relational/tests/check_control_oracle.py` and
`build/relational-control-order-oracle.json`.

The same **21,941 assertions and 3 + 16 Python test groups** also pass under
address, undefined-behavior and leak sanitizers, with no findings. The source
suite takes 28.963 seconds in the normal run and 355.179 seconds in the sanitizer
run; these are validation observations, not performance guarantees. Logs are
`build/relational-next-test.log` and `build/relational-sanitize.log`.

`make relational-control-msx` resolves
[`examples/relational_control.vx`](../examples/relational_control.vx), which
combines a recursive triangle sum, scalar overload selection, local updates
and a runtime conditional. All **256 byte inputs pass on both the host and the
Z80 simulator**, with four output payload bytes. Its editable SDCC/megalinker
project and provenance are retained in
`build/relational-control-msx/run-qbrxm78z/comparison.json`.
The existing serial/overlap/retained source projects also pass **753 host pairs
and eight Z80 pairs each**, with current evidence in
`build/relational-source-msx/run-74zli74i/comparison.json`.

The RFC now explicitly separates recursive type closure from value completion
and specifies that overload ambiguity/body validation cannot be treated as
optimization choices. The architecture describes the next required interface:
incoming state, result properties, outgoing state and completion as separate
invocation relationships. This remains a bounded replacement-core experiment;
the established frontend is unchanged.

## Execution-state interfaces and residual recursion

The next continuation implements that scalar call-state interface in
`experimental/relational/src/source_flow.inc`. Each source occurrence relates
incoming state to result properties, outgoing state and completion. Immutable
location maps point to relational value cells. Argument and statement order
build state edges; solver worklist order cannot change which store a load sees.
Arithmetic and conversions continue to use the existing relational clauses.

Nested block/call writes now return updated captures, while automatic locals
remain within their invocation. Runtime statement branches merge scalar state.
Constant recursion includes captured values in its specialization identity, so
changing captured state can make progress with unchanged explicit arguments.
Revisiting the same unfinished state still does not prove completion. Declared
return signatures can answer type queries independently; a result type selected
by a mutated capture observes the appropriate returned state.

Runtime scalar `&~` interfaces now produce resident C functions, including mutual
recursion and calls that complete without a value. Each call has automatic
result/capture storage. A completion trace preserves calls whose results are
discarded, including when the final source value is constant. Full logical-type
ranges enter recursive interfaces, preventing first-call values from becoming
false invariants. Private C output pointers are implementation details of the
resident ABI, with no source-language pointer addition.

The kernel gains a candidate-local witness operation. Artifact terms must close
before a model is publishable; duplicate names must unify, and rejected contexts
cannot leak generated functions into the chosen model. The other kernel change
checks fresh clauses' primitive guards before admitting a search alternative.
This preserves the same relationships while avoiding branches already refuted
by known facts. The combined control example, which initially exhausted the
unchanged one-million-step limit, now closes in **146,850 steps and 12 decisions**.

Normal verification passes **21,977 C++ assertions and 3 + 16 + 7 Python test
groups**. The new execution suite runs five runtime programs under six search
schedules and every byte input (**7,680 host executions**), plus conditional
call and completion instrumentation checks. It checks that a discarded recursive
result still executes exactly the expected number of activations. Unfinished
capture, global-state and aggregate-state interfaces cannot emit a C project.
The previous captured-write test now expects its computed value; a block ending
in a statement-only assignment is rejected as a value initializer. Existing
independent oracles pass **546 source cases and 4,608 control executions**.

The same **21,977 assertions and 3 + 16 + 7 Python groups** pass with address,
undefined-behavior and leak sanitizers, with no findings. Normal and sanitizer
logs are `build/relational-flow-test.log` and `build/relational-sanitize.log`;
independent oracle logs are `build/relational-flow-source-oracle.log` and
`build/relational-flow-control-oracle.log`.

`make relational-execution-msx` builds
[`examples/relational_execution.vx`](../examples/relational_execution.vx), whose
runtime recursion updates a captured accumulator and whose caller discards the
call result before reading that accumulator. All **2,304 admitted host inputs
and 54 Z80 inputs pass**. Its editable SDCC/megalinker project and provenance are
in `build/relational-execution-msx/run-yqq6xt3p/comparison.json`. The linked project,
including its harness, uses **2,420 resident bytes** and **two output payload
bytes**. The Z80 workload observes **391 stack bytes**, within the 1,024-byte
reserve; that observation is not a general stack bound. SDCC 4.2.0's Z80 port
uses automatic locals without the other ports' `__reentrant` attribute.

The existing control project again passes **256 host and 256 Z80 inputs**, with
evidence in `build/relational-control-msx/run-owc_0oko/comparison.json`. The three
lifetime projects pass **753 host and eight Z80 pairs each**, retaining the wrong
sharing counterexamples, in
`build/relational-source-msx/run-f7qxxx48/comparison.json`. Source, executable,
projection, plan and target-input hashes match the final normal executable.
Its symbols contain no `vx::Solver` reference.

This closes the scalar execution-state step, not the complete RFC. Aggregate
execution state, output-only runtime captures, persistent boundary state,
dynamic schema capture closure and activation-derived frame optimization remain
open. Runtime capture discovery is conservative and includes initialized visible
locals even when unused. The exact scope is recorded in
[the source adapter profile](../experimental/relational/SOURCE.md).
