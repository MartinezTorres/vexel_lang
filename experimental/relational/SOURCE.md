# The source-to-execution slice

`vexel-source` connects real Vexel syntax to the relational core and emits a
checked C realization. It reuses `src/parser.cpp` and the source arena. Its
link contains no `vx::Solver`, old type-equation machinery or old evaluator.
`build/vexel` is this same frontend; the earlier Solver is historical source.

## Run it

From the repository root:

```sh
make relational
build/relational/vexel-source examples/relational_source.vx \
  --query value:left_length --query value:right_length
build/relational/vexel-source examples/relational_source.vx \
  --query type:partial.count --then --query type:partial
build/relational/vexel-source examples/relational_source.vx \
  --query type:first.data --then \
  --source first --source second --budget 6 --mode serial --then --mode overlap
make test-relational
python3 experimental/relational/tests/check_source_oracle.py build/relational/vexel-source
python3 experimental/relational/tests/check_control_oracle.py
make relational-source-msx
make relational-control-msx
make relational-execution-msx
make relational-boundary-msx
make relational-operators-msx
make relational-bindings-msx
```

The first query resolves lengths 4 and 3. The second prints a resolved field
query followed by an unresolved whole-record query and exits with status 1.
The third prints three revisions: the source type alone, a serial realization,
and an overlapping realization. `--then` retains the accumulated questions and
applies subsequent options to the next revision. Each revision reconstructs its
obligations from the same parsed source snapshot. Previous physical decisions
are never premises of the next revision.

`relational-source-msx` creates fresh editable projects under
`build/relational-source-msx/run-*`. It compiles generated C with both the host
compiler and SDCC, links with the vendored megalinker, checks placement and runs
the cartridge with the existing Z80 test runner. It requires the same SDCC tools
as the shared backend. It uses the same frontend as `build/vexel`.

Add `--emit-c NEW_DIRECTORY` to a resolved request with runtime sources to
inspect `vexel.h`, `vx_bridges.c` and `source-plan.json`. Unfinished resolution
cannot write a projection. Existing nonempty directories are refused.

The `value` text in each reported source leaf is an expression preview, limited
to 4,096 bytes plus an ellipsis. A shortened preview carries
`value_truncated: true`. This keeps a compact shared graph from expanding into
an enormous diagnostic string. The resolved graph and emitted code retain the
complete value; structured query observations keep their existing format.

Solver graph sharing and target expression sharing are distinct. The typed
value and region emitters materialize selected common values. The ordinary C
emitter can still duplicate shared arithmetic in loop-transfer expressions;
general materialization across those paths remains open. It must account for
temporary lifetimes and frame/bank storage as well as expression equality.

## Shared value ranges

Closed projections also report `value_ranges`: bounded integer envelopes shared
across materialized arguments, captures, results and initialized loop state.
The per-function arrays list only leaves narrower than their declared native
domain. `carried` identifies loop/root/leaf ranges. `rounds` and `converged`
distinguish a fixed point from the safe partial result at the 32-round budget.
These are implementation facts about successful evaluations, not new source
types or completion proofs. `examples/range_transport.vx` demonstrates a masked
argument proving a predicate result constant across forwarding calls. Its
execution check is `python3 experimental/relational/tests/test_ranges.py
build/vexel --msx` (on one command line). Packaged MSX plans retain this report
alongside interface demand and activation facts.

The same function graph can also be queried with each invocation's own input
bounds. `value_ranges.call_results` lists `{site,function,leaf,min,max}` facts
that are tighter than the shared function's return envelope. These are bounds
across executions of that site, not recorded runtime values. The shared
function still uses the union of all callers to select its interface.
`value_ranges.contexts` reports the per-round function-context, value-query and
depth limits, the peak number of cached call contexts, and any budget hit.
Recursive queries and exhausted budgets use conservative general envelopes;
they do not fail semantic resolution or establish completion.

`examples/call_contexts.vx` demonstrates two captured-state updates with one
native implementation and separate result snapshots. Its host/Z80 check is
`python3 experimental/relational/tests/test_call_contexts.py build/vexel --msx`.

Selections and changing loop transfers can query their values under their own
guards. Integer comparisons, negation and forced short-circuit paths refine
the branch environment; sibling paths keep separate facts. Wrapping casts do
not narrow the original input unless the conversion is proved value-preserving.
These queries share the contextual term budget. `branch_depth_limit` and
`branch_depth_peak` in `value_ranges.contexts` report the additional nesting
bound; `budget_hit` covers both call and branch queries. Exhaustion retains a
general envelope. `examples/conditional_facts.vx` demonstrates shared byte
results with unrestricted 32-bit arguments. Its host/Z80 check is
`python3 experimental/relational/tests/test_conditional_facts.py build/vexel --msx`.

Invocation paths also constrain shared native arguments and captures before
private storage is selected. `value_ranges.call_inputs` lists guarded sites with
`site`, `function`, `paths`, `guarded_paths`, `excluded`, `arguments` and `captures`.
Each bound uses `{leaf,min,max}` and is listed only when narrower than the
declared native domain. `excluded` means established facts excluded every
recorded path for the input union; it is not an instruction to remove execution.
All callers contribute to one shared interface. Unrecognized predicates and
budget exhaustion retain conservative bounds; paths keep at most 32 premises.
Loop body calls use the current condition, while condition calls are evaluated
before that test. Mutable enclosing facts do not cross loop boundaries without
a preservation proof. `examples/guarded_inputs.vx` demonstrates byte arguments
through forwarding helpers with unrestricted public inputs. Its host/Z80 check
is `python3 experimental/relational/tests/test_guarded_inputs.py build/vexel --msx`.

Structural value queries share abstract answers within identical function and
branch environments. `contexts.node_peak` records peak contextual evaluations;
`environment_peak` records peak distinct input/branch environments per cache
generation. `query_hits` and `query_misses` count reused and evaluated nonliteral
queries across the projection (including general queries and final simplification).
Cache hits do not spend the contextual node budget. Refinement rounds and
changes to temporary readiness hypotheses invalidate answers. Separate call
snapshots, owners and guards remain distinct; execution is never memoized.
`examples/shared_proofs.vx` exercises shared guards across multiple result fields.
Its host/Z80 check is
`python3 experimental/relational/tests/test_shared_proofs.py build/vexel --msx`.

These bounds also select private storage widths. `runtime_functions` reports
`transport_types.arguments`, `.captures` and `.results`, each with a logical
leaf index, `logical` source type and `storage` type. The transported-byte
counts use the selected storage. Each `interface_demand.loops` entry similarly
reports `transport_types`, `retained_logical_bytes` and `retained_storage_bytes`
for its retained cells. These loop counts exclude readiness/index fields and
count one state image; they are not whole-frame or stack bounds.

`examples/transport_widths.vx` keeps 32-bit source arithmetic while reducing
private arguments and results to bytes. Ordinary and banked C use the same
layout, as does private carried state in region bindings. Public entry and
explicit C binding types retain their contracts. Check host and Z80 behavior
with `python3 experimental/relational/tests/test_transport.py build/vexel --msx`.

Private storage can also carry only the bits observed by every consumer, even
when the logical value has a wider range. `examples/bit_demand.vx` follows that
demand through a call chain and a loop. The shared closure joins byte, word and
full-width demands before choosing any layout. A wider caller strengthens the
same interface; it does not depend on which declaration was discovered first.
`runtime_functions.demanded_bits` lists requirements for logical arguments,
captures and results (zero means unobserved). Each loop also reports
`demanded_bits` in original carried-leaf order.

These narrower carriers hold unsigned residues. They do not claim that the
original value lies in a smaller interval. Range-based carriers can instead
reconstruct a complete value, including its sign. Calls and parallel loop
transfers explicitly convert at the physical boundary, allowing their
producer calculations to use the required width. Conditional transfers retain
their evaluation sites: conversion can enter each arm, but a calculation or
snapshot from an inactive arm is never hoisted.

Mask, constant-shift and power-of-two division/remainder transfers account for
the particular observed bits. Unknown shifts/divisors, comparisons, truth tests,
foreign observations and other full-value consumers retain what they need.
Execution, trap checks and loop readiness remain separate demands. Host and
target coverage is in `tests/test_bit_demand.py`; public and foreign ABIs remain
unchanged.

`examples/constant_division.vx` demonstrates nonzero constant division and
remainder in the shared integer graph. Byte, word and dword operations can use
specialized arithmetic; byte-dependent rules can also use finite ROM tables.
Signed division truncates toward zero, signed remainder retains the numerator's
sign, and minimum/-1 wraps. Variable divisors can use width-specific leaves
when the shared range transfer proves them nonzero after conversion. Zero
divisors remain invalid; an unproved divisor retains its checked realization.
Proved valid variable shifts likewise use native operations, with zero retained
bits when a valid left shift exceeds a reduced physical width.
These alternatives apply to ordinary C and
pure bindings. `python3 tests/test_constant_division.py ../../build/vexel --msx` checks both
against an independent oracle, including all byte inputs and signed boundary values.

The shared interval transfer derives successful-value bounds for nonzero
divisor intervals and bounded shifts. Those facts cross calls and loops like
other envelopes, narrowing storage and predicates where justified. A narrower
result does not erase a possible trap in an operand or condition.

## Whole programs and publication

```sh
build/vexel game.vx --program --mode serial --emit-c build/game-image
```

`--program` requests all exported defined functions (`&^`) as runtime entries
and all exported values (`^`) as `value:` observations, including imports. The
solver resolves their shared dependencies and physical constraints in one
request. Extra queries/entries and metadata-selected entries remain additive.
Explicit named occurrences replace the implicit occurrence of that definition.
Private, unused declarations remain undemanded. An open export prevents a closed
program image; there is no best-effort omission. A values-only program can
resolve, but C emission still requires a runtime entry.

This is a membership contract, independent of activation. `--mode serial` means
the adapter consumes a result before the next activation; it does not prove
that an external callback cannot re-enter. Existing occurrence, stream, boundary
and `c_binding` lifetime facts still govern storage reuse. The complete request,
including program selection, participates in workspace cache identity.

Every emitted projection, including selective requests, carries
`program-image.json` and the identical `program_image` object in its source plan:

- `format`: `vexel-program-image-1`; `selection`: `all-exports` or
  `selected-observations`; `activation`: the requested boundary profile.
- `roots`: occurrence name, source definition and owning source unit;
  `observations`: explicitly selected or program-discovered query names.
- `units`: symbol, kind (`source`, `native`, `boundary`, `helper`), fragment,
  activation `owners`, known generated `calls`, and optional private `support`
  header (otherwise `null`). Include that header with the fragment when placing
  a unit in a separate C translation unit: it owns the demanded static arithmetic
  helpers. These are compiler facts, not a general C parser or an exhaustive
  target-library dependency graph.
- `segments`: fragment paths in emission order. Concatenating them reconstructs
  `vx_bridges.c` exactly. A source unit includes its result reader and stream
  wrappers. `support` combines the other segments for one shared compilation.
- `artifacts`: generated paths, byte counts and FNV-1a content fingerprints,
  including typed bindings, bank modules and continuations. The image and source
  plan are excluded from this inventory to avoid self-referential hashes.

`experimental/relational/program_image.py` validates ownership of the image by
the plan, all inventoried products and complete C reconstruction. Fingerprints
detect stale/mixed products; they are not cryptographic attestations. Packagers
may distribute units according to their embedding contract. Shared support and
declarations still need that contract; unit membership is not permission to move
arbitrary code between hardware windows.

Resolved query rows retain their printable `answer` and add `term`: numbers are
`{"number":"decimal integer"}` (including arbitrary precision), and constructors
are `{"constructor":"name","arguments":[...]}` recursively. No unresolved
variables are published as closed observations. Build adapters can consume
tuples/strings without parsing the diagnostic answer notation.

`make test-examples EXAMPLE_OUTPUT=build/new-example-check` checks every `.vx`
example as a whole program and runs the supported host/Z80 drivers. Reports and
images remain in that directory. Two historical drivers for the retired frontend
are identified explicitly in the summary; their source examples still receive
the whole-program check. `--baseline OLD_EXECUTABLE` on the checker additionally
compares discovery against explicit requests to an earlier executable.
The type-equation example currently retains one known open value export,
`partial`: its selective `type:` query resolves but whole-value closure does
not. The checker records this existing limitation separately instead of treating
it as a closed image. Target workloads use the serialized SDCC resource guard;
memory or time exhaustion is a rejected build candidate.

The bank projection records each module's original `partition_owner`, its
`partition_role` and effective `chunk_operations`. Initial MSX realization uses
those facts to retry only affected functions after an oversized bank or guarded
SDCC failure. Scoped `prefer` keys such as `bank_chunk_operations.vx_entry`
combine with the global limit by minimum. All attempts still resolve the whole
selected program; source/resource changes invalidate measurements before another
build. This is local physical feedback, not yet incremental semantic solving or
reuse of compiled objects across candidates.

## Incremental sessions

Run `build/vexel --session` (or `build/relational/vexel-source --session`). The
process accepts one command per input line and returns one JSON line per request:

```text
solve "examples/incremental_msx/game.vx" --source frame
solve "examples/incremental_msx/game.vx" --source tone
solve "examples/incremental_msx/game.vx" --source frame --emit-c "build/frame-new"
clear
quit
```

Arguments containing whitespace must be double-quoted; inside quotes a backslash
escapes the next character. There is no shell expansion. Blank lines are ignored;
`quit` or EOF closes the session. `clear` releases retained results and replies
with `{"status":"cleared"}`. Errors reply with `status: "error"` and `error` text;
the session stays available for a corrected request. Session exit status reports
transport/startup success, so clients must inspect each reply's status. `--then`
is unavailable here: submit another `solve` line instead. Options start from
defaults on each line, rather than accumulating across lines.

Finish edits before submitting the next request. Every miss takes a new input
snapshot. Up to 32 resolved groups are retained, with least-recently-used
eviction. A group is an entry path plus its full request and search/target
options. Its support includes every parsed source/import file, exact bytes of
demanded resources, observed directory membership, import kinds, missing-path
facts and symlink destinations. A source edit rebuilds all requested groups
with that file in their import closure, even if the edit is in an unused body.
Resource dependencies are tracked per request: editing `palette.txt` invalidates
`frame` but leaves `tone` reusable. A joint frame/tone request is one group;
the cache does not split a physical plan into independent subplans.

Content equality is checked directly, including same-size edits with unchanged
timestamps. A cached negative capability therefore cannot survive a signature
change. Unresolved, ambiguous, contradictory and exhausted results are not
retained. Process outputs have undeclared external inputs, so requests that
observe them are recomputed; a type-only query need not run the command. The C++
API also bypasses reuse for caller-owned shared search budgets. `SourceWorkspace`
wraps the unchanged `SourceProgram` API; `--then` continues to use a fixed
source/process snapshot and does not observe edits.

Every solve reply adds `incremental`: `reused`, the group's last solved
`revision`, `solver_work`, `verification_work`, `verified`, `cache_entries`,
`reason` and `invalidated_inputs`. `stats` describe the retained derivation;
`solver_work` is zero on reuse. Support checking and file emission still take
time. Revision identifiers are local to the workspace, advance on fresh solves,
and are retained on reuse. They are not global input version numbers.

`--session --verify-incremental` solves cache hits afresh and compares their
entire report and generated files. Verification failure discards the cached
entry and reports an error. Misses already use fresh solves, and process results
are never cache hits, so this check does not execute a command twice.

`--emit-c` works on both misses and hits, retaining the normal new/empty-directory
rule. Generated `source-plan.json` describes the semantic derivation without the
session bookkeeping, so its bytes agree with a fresh emission. MSX packaging
still validates provenance, and target measurements belong to the built ROM.
`make incremental-msx` runs the edit replay, host oracle, SDCC/megalinker build and
Z80 oracle, recording ROM/RAM/observed-stack costs in a fresh run directory.
`make test-incremental` runs the edit, path, resource and module checks. For a
focused sanitizer run, use
`make sanitize SANITIZE_TARGET=test-incremental SANITIZE_LOG=/tmp/vexel-inputs-sanitize.log`;
omitting `SANITIZE_TARGET` runs the complete suite.

## The source is the model

The central example contains these ordinary Vexel relationships:

```vexel
left:#u8[|right|+1];
right:#u8[7-|left|];

&packet(offset:#u8,count:#u8,x:#u16) {
    #Packet(count:#u8,data:#u16[count+offset]);
    #Packet(count,[x,x+1,x+2])
}

&^first([[range(0,250)]] x:#u8) {packet(1,|right|-1,(#u16)x)}
&^second([[range(0,250)]] x:#u8) {packet(2,|left|-3,(#u16)x)}
```

The lengths have no initializer seeds. Their equations determine them jointly;
the solved lengths then constrain the generic record applications. Each call
has its own field relationships and frozen capture of `offset`. The initializer
must satisfy the original dimension expression. A capture that makes the
dimension disagree rejects the whole record.

`[[range(lower,upper)]]` is an experimental boundary-adapter annotation using the
RFC's existing metadata syntax. It adds no keyword. Its bounds must fit the
parameter type, and the generated producer checks them. Omitting it uses the
type's full signed or unsigned range. Narrow storage depends on this enforced premise,
not on the inputs selected by a test.

## Property demands

Each source cell can contribute type, value, length, element, outer-category and schema
obligations. A demanded property gets one relational variable before its
dependencies are traversed. Encountering it again relates that same variable;
it does not recursively try to evaluate a missing initializer. The adapter
constructs equations, and the core solves them.

Delayed construction and control-proof caches index immutable term structure
directly. Cache keys preserve DAG sharing and distinguish constructor, number
and variable identities exactly; no pretty-printed expansion or probabilistic
digest is used as evidence of equality. The index does not cache a binding's
resolution: revised inputs have their own structural keys, while unchanged
closed inputs can reuse the same construction. Its identities are private to
one index lifetime; they do not enter generated interfaces or diagnostics.

Loop-initialization closure likewise preserves sharing while replacing stable
slot values, collecting numeric dependencies and classifying changing values.
Each memo belongs to one resolved loop context. Numeric dependency discovery
still reaches its fixed point, and back edges still forget changing predicates;
the traversal reuse supplies no additional initialization or invariant facts.

The Boolean proof graph likewise stores atom identities and exact node/cache
triples instead of copying atom text into each key. Hashing only locates a
candidate key: equality still compares every triple element. Atom ordering
continues to use the original lexical names, comparing large names as streams
over shared structure. A bounded preview accelerates small-name comparisons;
it never defines identity. Distinct structures with identical printed spelling
have separate atoms and a structural ordering tie-breaker. Loop renaming uses
primitive bit handles belonging to that proof graph.

Boolean handles represent complement as an edge bit, allowing a function and
its negation to share physical nodes. The 100,000-node limit counts this storage.
Quantification, substitution and term reconstruction preserve the full logical
relation, including negated functions; the complement is not an extra premise.

Comparisons of conditional native constants also have a bounded direct proof.
When both alternatives give the same Boolean answer, their selecting condition
does not need bit-level expansion. The comparison retains its width, signedness
and operand order; a scan exceeding 256 choice/value nodes or encountering an
unsupported value falls back to ordinary word reasoning. Execution obligations
remain separate, including traps preceding a known result.

Optional conditional answers are retained only within the source session that
constructed them. Keys include ordered premises and interpretation mode;
recorded native interpretation dependencies are checked again before reuse.
Changed dependencies invalidate the answer, and exhausted attempts are not
cached as answers. Native certificate retries can separately reuse their
constructed request, but every retry solves it with fresh bindings and current
parent-fact checks. The report exposes `conditional_proofs` and `native_proofs`
counters to make reuse, exhaustion and retained work visible.

Native proof retries normally receive 20,000 work units. Powers-of-two attempts
receive a larger allowance, capped at 160,000 and the caller's own limit. The
intervening attempts remain cheap and recheck current inputs. This is a bounded
effort policy, not a cache of failure or missing evidence. Reports include actual
work and boosted attempts so its tradeoff remains measurable.

A bounded modular proof can also close full-width arithmetic identities when
the ordinary Boolean proof exceeds its node budget. For example,
`examples/modular_control.vx` proves commutativity, distributivity and cancellation
over 32-bit words before emitting a single addition. Narrowing and sign extension
remain part of the relationship; unknown calls remain distinct values. The
certificate cannot remove operand initialization, runtime effects or traps.

These algebraic relationships also feed physical interface demand. Equally typed
function results and changing loop values can share transport after equality is
proved at the same return or loop clock. Conditional transfers use separate truth
atoms, preserving the distinction between an integer and its nonzero test.
Constant or single-atom reductions can expose immutable aliases through native
calls and loop induction; their narrowing or sign conversion remains explicit
until the ordinary type facts justify removing it. The solver emits neither
expanded polynomials nor speculative branch evaluations. Existing execution,
readiness and completion obligations remain in force.

A field query follows the field relationship without demanding the whole record.
A whole-record query adds its schema requirements and captures. Reading a
field's value also requires its producer and record validation. This distinction
applies to both source expressions such as `#[partial.count]` and CLI queries.
The same separation applies to arrays inside records and annotated call results.
An array category does not require its element or extent. Consequently `|x|`
can select the length relationship while its extent is still being solved;
it selects ordinary operand execution for scalar absolute value. A bare record
requirement can expose an independently known field without an initialized
record value or a closed sibling schema.

Direct cyclic type aliases join initializer or argument interfaces without
choosing one declaration as the anchor. The equality can be established through
an ordinary helper using an isolated opaque proof. A member projection retains
the logical owner, including its joined interface: it never substitutes the
owner's original initializer for the resolved field type. Record identity,
member type and full schema are separate demands. Delayed discoveries remain
inside the same assumption boundary, and nested proof requests share search
fuel. Transforming query equations revise a candidate vector in isolated
requests, using ordinary typing and overload rules for every revision. All
supplied witnesses are reconsidered, and each original query must equal the
final candidate. Provisional errors cannot prevent another equation from
contributing a revision. Named proof inputs allow constructed fields to wait
for facts from their enclosing execution without borrowing its binding store.
`make relational-types-msx` checks this behavior in generated host and Z80 code;
see the completion ledger for the verified checkpoint and remaining limitations.

Capture discovery follows properties actually read by a schema: `#[x]` captures
x's type; `|array|` captures its length; arithmetic may require both a scalar's
value and type. Captured types and lengths do not force unrelated values to be
initialized. Field-owned dependencies terminate capture discovery because they
belong to the record instance, not its lexical schema.

Unsupported demanded syntax contributes an unfinished obligation or an explicit
adapter diagnostic. It is never silently delegated to the old solver. An
unfinished query is not changed into runtime dependence.

## Overloads, recursion and local state

The next [example](../../examples/relational_control.vx) combines four features:

```vexel
&~triangle(n:#u16) {n==0 ? (#u16)0 : n+triangle(n-1)}
&step(x:#u8) {x+1}
&step(x:#u16) {x+2}
base=triangle(3);
&^control(x:#u8) {
    state:#u16=(#u16)x;
    before=state;
    state+=step(base);
    state=x<128 ? state*2 : state+1;
    [before,state]
}
```

`base` resolves to six. Its logical `#u16` type selects the second overload,
which yields eight. `before` retains the original state. The final result is
`[x, 2*x+16]` below 128, and `[x, x+9]` otherwise. Four payload bytes suffice.
`make relational-control-msx` checks all 256 byte inputs on the host and Z80 in
a fresh editable SDCC/megalinker project. Recursion here is resolved before
execution; the conditional and input-dependent arithmetic are emitted as C.

Two general core operations support this expansion. A **shared obligation
bundle** contributes its relationships once per search context. A **builder**
waits for a declared input prefix to become ground, then constructs further
obligations. It cannot inspect or modify solver bindings. Builders make source
decisions from the input facts they explicitly receive; numerical evaluation
and type relationships still belong to ordinary clauses and constraints.
Bundled relationships are reactivated in every alternative and every revision,
even when their source construction is cached. A cyclic bundle is not itself
evidence of value completion.

For supported scalar overloads, argument relationships calculate applicability
and specificity for the complete declaration set: an exact type match scores
two, an allowed conversion one, and an unannotated parameter zero. An
inapplicable argument rejects that candidate. The unique greatest total wins.
Only then does a builder connect the selected body to its consumer. Equal best
scores are ambiguous even if the bodies would return identical values. A
result annotation cannot choose a winner; a failing winning body cannot enable
a less specific fallback. Unsigned and Boolean annotations, generic parameters,
arity and exact-integer representability are covered. Structural and dependent
annotations use the same ranking after their constituent requirements close.
Partially closed cyclic interfaces and methods remain open.

`&~` calls with known scalar arguments instantiate invocation properties.
Direct and mutual recursion can reach base cases through delayed conditionals.
Known conditions demand only their selected branch. Re-entering an unfinished
value invocation with the same argument and captured-state values stays
unresolved; changing a captured value can supply progress even when the explicit
arguments are unchanged. A growing chain consumes the work or source
construction budget and reports exhaustion. Neither is interpreted as a
completed constant, including when its consumer multiplies the result by zero.

Type and value invocation identities are distinct. An annotated return type
can answer a type query without executing the call. For an unannotated runtime
signature, unsigned inputs retain their logical types while their changing
residual expressions are abstracted. Shared recursive type equations can close
against a base branch. Value demands with runtime arguments or captures now
materialize a scalar call interface. Compile-time values that affect the
return type remain part of specialization identity. A result type selected by
an execution-dependent condition observes the state returned by preceding
calls; a declared signature does not require their execution to finish.

Unannotated functions can now expose a common result type with unknown native
arguments. A private request executes the semantic relationships with symbolic
native inputs and checks every return/completion path. Only a closed type that
contains none of those symbolic inputs is published. The proof imports required
parent facts explicitly and shares the enclosing search budget. A failed proof
falls back to the actual call; it does not reject a known condition selecting a
valid branch with a different type. Direct partial-property relationships take
precedence, so a field query does not close unrelated argument fields. Type-only
proofs generate no source accesses or runtime functions. Native recursion,
nested calls and scalar/array/record/tuple result interfaces have executable
coverage; automatic captures, receiver/expression parameters and non-native
argument leaves still use the existing concrete route.

This abstraction is a sufficient proof, not quantification over source choices.
It cannot discard a finite alternative merely because a runtime prototype lacks
a concrete array extent, then publish the surviving alternative as a universal
type. Private interface and capture proofs therefore defer newly demanded finite
choices and transforming type-choice bundles to the original joint request.
Supplied caller facts remain usable. An inconclusive abstract proof retains the
concrete call's obligations; ambiguous concrete calls remain ambiguous. More
general parametric proofs across source alternatives are still open.

Straight-line scalar mutation creates explicit locations and store versions.
Loads retain their reaching store; later assignments cannot rewrite an earlier
read. Each store converts its value to the location's logical type. Parameter
updates belong to the current invocation. A conditional on the right of a
store supplies a value merge; runtime branches must have equal logical types.
Nested captures see the caller's reaching version of their lexical location.
Writes across block/call boundaries and inside conditional statement branches
now carry an explicit incoming/outgoing state relationship. There are no
implicit thread or interrupt categories.

## Execution state and runtime calls

An execution projection connects a source occurrence and its incoming state to
three separate promises: its result properties, outgoing state and completion.
A state identifies an immutable map from logical locations to reaching-value
cells. These cells contain relational promises; the map is not a host-language
store of evaluated Vexel values. Arithmetic and conversions still use the
ordinary source clauses. Statement and argument order determines state edges;
the order in which the solver discovers facts does not determine that order.

Calls return updated captures to the caller and discard their automatic local
locations. Earlier reads retain their original value cells. Runtime branches
join scalar state and carry conditional execution traces. A statement can
complete without producing a value; the internal `NoValue` term adds no source
keyword or user-visible unit type. A discarded call result does not discard
its execution. Even a constant final result retains the trace that precedes it.

Observable uniqueness follows the closed residual graph. A native call handle
alone does not determine its result: the observation includes reachable function
completion, demanded return/capture leaves, and persistent initializers. Empty
aggregates and `NoValue` sources retain their execution roots. A finite choice
that changes a sink invocation is therefore observable even without payload.
Unused native return leaves remain unobserved, and a type-only query does not
observe an unused implementation. Recursive references close through a finite
definition graph. Internal frame, temporary and loop identities are canonicalized;
native output indices are mapped back to logical leaves, and backing-buffer
sizes do not participate in semantic comparison.

This normalization belongs to observation comparison. Exported private C names
may still reflect discovery order. Whole-program schedule checks compare
returned values and ordered effect traces; byte-identical generated files are
not required across different schedules.

The relation machine exposes a generic observation-closure hook over completed
candidate terms. It cannot inspect bindings or discharge unresolved obligations.
The source adapter supplies the graph interpretation and keeps the original
artifacts for C generation. This compares normalized residual relationships;
it is not a decision procedure for arbitrary program equivalence.

The [runtime example](../../examples/relational_execution.vx) exercises this:

```vexel
&^accumulate([[range(0,8)]] n:#u8, seed:#u8) {
    acc:#u16=(#u16)seed;
    &~sum(k:#u8)->#u16 {
        k==0 ? acc : {acc+=(#u16)k;sum(k-1)}
    }
    sum(n);
    acc
}
```

Each recursive call receives and returns the accumulator's logical location.
The call's value is discarded, but its state change reaches the final read.
Unlike the preceding `triangle(3)` example, this recursion remains in the
generated C. `make relational-execution-msx` checks all 2,304 admitted host
inputs and 54 Z80 cases covering every admitted depth and six seed values.
The expected result is independently calculated as `seed+n*(n+1)/2`.

Runtime definitions are keyed by declaration, lexical capture identities and
concrete scalar interface types. The definition is registered before its body
is related, so mutual recursive references do not expand forever. Known argument leaves remain specialization facts; runtime arguments enter
with their full logical ranges. Native captured values retain those ranges
rather than the first invocation's value. Non-native captures and results
retain static facts, including strings and wide constants. Only native leaves
occupy C ABI slots. Returned native ranges remain conservative. Each call
occurrence owns its result/capture temporaries.

Native calls now try a semantic capture-closure proof before constructing their
ABI. The body uses lazy capture prototypes: native storage spans its full logical
range, while strings and other static facts retain their supplied values. This
lets ordinary overload resolution eliminate an unrelated method's unknown
capture, including through nested native helpers. Output-only writes and facts
used in type dimensions also contribute capture requirements. Known arguments
retain invariant constants; changing native constants become runtime slots at
recursive edges, while changing static shapes/types require an explicit interface; a captured native flag cannot be specialized to the
first caller's value.

The proof observes candidate-local location-use artifacts, including unused roots.
It accepts a smaller capture set only after the body closes and every completed
alternative agrees on that set. Failed or ambiguous proofs retain the original
capture obligations; unfinished parent inputs are requested through explicit
property promises. Frame caches distinguish private proof instances. The
`capture_proofs` report records up to 256 proof attempts, including retries and
discarded alternatives; the final `runtime_functions` list describes the emitted
interfaces. Expression parameters use the existing capture route.
Frame keys retain enclosing recursive interface assumptions, so a helper built
from another source root cannot hide an incompatible back edge. Equivalent
helpers in different contexts may consequently have distinct physical frames.

Recursive capture cycles use one conservative interface containing their
lexically reachable state. If ordinary relationships cannot close it, supply
initialized/annotated state or pass it through explicit typed parameters and
results. The old enumeration of omitted, type-only, input and output proposals
has been removed, along with omitted-root assumptions. No private proposal can
make an uninitialized input available. Ordinary nonrecursive inference still
finds narrower capture modes. The `capture_proofs` report records inference
attempts; it no longer reports speculative recursive interfaces.

The capture proof distinguishes location use, incoming-value dependence and
writes. A location may supply only its reaching type, with no initialized value.
Read-only captures occupy no returned slots; unconditional whole-root stores
need no incoming value. Partial stores still demand the previous aggregate.
These modes participate in the frame key and closed witness observation, so
they cannot be taken from an unfinished candidate. Nested type-only captures
retain their original location identity and reaching type. Loop storage keeps
type information separate from initialization, including first writes inside
nonempty loops. `runtime_functions` reports `input_captures` and
`output_captures` as counts of logical roots; static leaves occupy no native
ABI slots. The conservative recursive interface need not be the smallest ABI.
An unresolved recursive result promise asks for a return annotation or a
grounded result relationship; annotations do not assert termination.

The C projection first proves immutable constants and aliases across native
calls and loops. Result summaries contain only constants or unchanged formals;
a caller may use an alias only when its actual is immutable after conversion.
Mutable actuals retain return snapshots. Knowing the eventual result alone does
not remove the materialized execution. Loop invariants are anchored at entry
and checked simultaneously against all transfers, with failing candidates and
their dependants removed. Always-ready loop roots need no readiness storage;
partially initialized roots keep their guards. The proof algebra handles simple
integer identities and selections, preserving possible trapping evaluation.
It is not an arbitrary symbolic evaluator, termination proof or cost optimizer.

Equal changing values can share storage too. The projection groups equally
typed, always-ready loop leaves with equal entry values, then refines these
hypotheses against every transfer. A failed group forces its dependants to be
rechecked. Only total transfers are eligible. Equal native result/capture leaves
share a member at the same invocation's return; result equality propagates
through forwarding calls, and alternates with loop and immutable-value proofs.
Different invocation sites remain distinct snapshots. This establishes equality
at specified execution points; it does not infer arbitrary algebraic identities,
recursive result equalities without evidence, or equality across observation times.

The ordinary C projection closes one demand graph over native arguments,
captures, results and loop-carried state. Observed source leaves, lazy initializer
values, control decisions, executed effects and potentially trapping evaluations
seed the calculation. A required result follows producer dependencies; a required
formal follows actual values from every caller. Loop-carried values follow entry,
condition and body transfers, including readiness. The same work queue handles
forwarding chains and recursive cycles; pure forwarding alone creates no demand.
Only after closure are unused physical arguments and carried leaves removed.
Loop counters are omitted when no remaining expression observes their index.
Parallel loop transfers and readiness checks retain their original behavior.

An unused result with unproved totality is evaluated without storing its value.
Potentially trapping actual arguments retain their original call positions;
potentially trapping loop transfers retain their guarded slots. Loop-return
payloads remain conservative consumers. Unproved completion remains mandatory.
Foreign calls keep full reply buffers and validation, even for ignored replies.

A separate execution certificate can prove a private function total and free
of effects. It covers all logical results and finite traces, closing through
already certified callees from a finite anchor. Loops, recursive cycles, foreign
calls, storage, checks and unfamiliar operations keep their obligations. A call
can then disappear if none of that invocation's results is observed and all its
actual evaluations are also discardable. A different caller may still need the
same shared body. Both ordinary C and alternate value/region interfaces consume
the certificate. `runtime_functions.discardable_execution` and
`interface_demand.discarded_calls` report the result; they are derived facts,
not unchecked purity or termination annotations.

The scalar certificate uses immutable expression bounds to admit division and
remainder by a proved nonzero divisor, and shifts with counts below the original
width. Thus `n/(n|1)` and `n<<(n&7)` for a byte can be discarded when their results
are unused. Divisor narrowing participates in the proof, and every operand must
itself complete. An unknown divisor, an out-of-range count or a trapping inner
expression retains its evaluation. These facts also remove duplicate validation
evaluations before ordinary C computes a demanded arithmetic result.

Closed caller and capture envelopes participate too. For example, an unused
private `1000/d` is removable when every caller supplies `(#u16)n+1` for a byte
`n`. Adding a caller that can supply zero preserves the division. Lazy value
arms may use their own guard premises, while their condition remains obligatory.
`runtime_functions.discardable_results` reports the per-logical-leaf certificate
shared by demand analysis, frame planning and emission. It does not replace
foreign validation, argument evaluation or producer execution certificates.

[`examples/execution_facts.vx`](../../examples/execution_facts.vx) makes the
distinction visible: one unused pure call disappears, a sink still observes the
input, and a subsequent unused division still traps on zero. Another invocation
of the pure function supplies the returned value.

The value-safety and loop-stability checks walk shared term nodes once per
query. A compact repeated expression therefore stays compact during these
checks too; a possible trap in its shared leaf remains mandatory.

Retained native return/capture leaves use 8-, 16- or 32-bit members of a private
`vx_flow_ID_result` struct, respecting signed and numeric representations.
NoValue's dummy slot can disappear; a completion-only native call has no result
pointer at all. A call with no remaining inputs has a `(void)` signature. Bank
continuations understand these structs and retain them through page changes.
The target checks their SDCC layout at compile time; host padding is harmless.

`runtime_functions` now also reports `logical_result_slots`,
`transported_result_slots` and `transported_logical_leaves`. The first counts
the original flattened return/capture interface, including its NoValue dummy;
the last lists retained logical indices in physical slot order.
`logical_argument_leaves` and `logical_capture_leaves` count the flattened
runtime input interfaces before pruning; `transported_arguments` and
`transported_captures` list their retained original indices. Static facts already
specialized by the semantic solver are outside those lists. The
`transported_result_bytes`, `transported_argument_bytes` and
`transported_capture_bytes` fields sum target scalar storage widths, not C ABI
overhead or stack bounds. `interface_demand` reports dependency counts and each
loop's original/retained carried leaves and iteration-counter requirement.
`known_results` lists each proven result leaf and its constant or formal alias;
this is a summary, not a promise of transport elimination at every call. One
mutable caller can still require the shared result member. Loop entries also
report `invariant_leaves`, `always_ready_slots` and `retained_readiness_flags`.
The last counts current flags; each stored flag has a parallel-transfer staging
byte. Proven counts include unused logical state, not only physically live state.
`result_aliases` maps logical return/capture leaves to canonical logical
representatives. `transported_logical_leaves` now lists the representatives with
physical members; an observed alias can require a formerly unobserved
representative. Known results can still require no transport. Each loop reports
`carried_aliases` as logical root/leaf pairs and `coalesced_carried_leaves` as the
number of aliases. These are equality proofs, including unused logical state;
they can overlap immutable facts and are not an independent byte-savings count.
The frame planner runs after this closure and layout. Internal `vx_flow_*`
signatures and buffers are generated details; edited C/ASM must follow the
generated headers, stores and mapping report. Public source and foreign ABIs
retain their contracts.

The core's generic **witness** operation retains a ground artifact within one
candidate. Runtime definitions and completion traces use it; emission reads
only the selected model's artifacts. Constructing a function in a rejected
candidate cannot cause that function to leak into generated C. Incomplete
artifacts prevent resolution. Construction caches share relationships, never
a candidate's bindings or an already selected execution.
An isolated request may list `observedWitnesses` to include selected artifacts'
presence and values in its uniqueness question. Absence becomes evidence only
when all obligations in the candidate have closed. An unfinished alternative
therefore cannot establish that a capture is unnecessary.

The resident C realization uses automatic frames for these calls, including
recursive result/capture buffers. Their private output pointers belong to this
C ABI, not the Vexel language. SDCC's Z80 port uses automatic parameter/local
storage by default; its other ports' `__reentrant` attribute is not emitted.
See the [SDCC guide](https://sdcc.sourceforge.net/doc/sdccman.pdf), sections 3.6
and 4.3.3, and inspect the project's generated C and assembly.
A positive `--frame-ram-bytes N` permits static local buffers when the selected
call graph excludes overlapping activations. Recursive functions and ancestors
of open boundary edges retain automatic locals. Selection respects the supplied
RAM budget and records its reasons; it does not prove a worst-case stack bound.
A zero budget retains the default editable projection.

Schema capture discovery follows candidate-local support edges, including
delayed overload and conditional construction. A whole schema closes only
after those edges close. A solved dimension alone cannot justify publishing an
incomplete capture identity. Fixed-layout relationships are checked on record
construction, stores, persistent initialization and loads, and external returns,
including discarded values. Invalid runtime relationships trap with code 19
before a write commits. Schema helpers receive readonly field and lexical
views; scratch locals remain mutable. Captures include branch alternatives
that later field updates can select. General branching dimensions over exact
fields and broader cyclic property interactions still require work.

## Persistent state and external boundaries

An uninitialized global denotes persistent storage, not an unknown constant.
A pure unresolved equation such as `x:#u8=x;` remains an unknown constant.
Initializer effect promises classify declarations independently of their final
values. A generic Boolean disjunction can propagate a required effect before
its other inputs close, but keeps their validity obligations alive. Conservative
signature proofs can expose an unconditional external operation before an
earlier cyclic read closes. No candidate publishes an unfinished initializer.

Constant initializers define immutable equations. Nonconstant initializers
execute lazily, once, before the first access. Generated storage tracks absent,
ready and initializing states. An uninitialized read traps with code 17;
reentering an initializer traps with code 18. Assignment evaluates its RHS
before initialization required by its write. Compound updates snapshot their
old operand before the RHS, while retaining RHS changes to other fields.
Partial writes use logical leaf masks, not source pointers.

Persistent and external reads always create new runtime snapshots. Earlier
stores cannot turn them into compile-time constants. External calls are never
executed by the host resolver. Their editable ABI is
`vx_external_NAME(uint32_t *result, ...typed argument leaves)`; record leaves
are flattened in declaration order. The result buffer belongs to the caller.
Generated wrappers validate returned scalar ranges. A void sink writes no
result leaves, and its source needs no payload buffer.
Record results also retain their original schema obligations; passing scalar
range checks alone does not admit an inconsistent external record.

External storage exposes typed volatile `vx_external_NAME_N` leaves in the
generated header. Backend bindings support `[[msx_address(N)]]` and byte-sized
`[[msx_port(N)]]`, with host shadows for execution tests. Binding ranges and
types are obligations checked before emission. A type-only query does not
demand a runtime access or execute an initializer.

The selected plan reports `boundary_storage` and `source_accesses`. Access
summaries follow materialized callees and initializer dependencies, including
recursive graphs. Serial and retained-output contracts order producer execution.
An overlap contract can use independent persistent resources, but shared writes
or lazy initialization need an access-order contract before emission. External
operations share a conservative `external-world` resource until a finer sink
contract is supplied. These are finite activation contracts. Explicit occurrence graphs below
add result ownership and repeated-source activations. Bounded stream pools and
conditional assembly stack bounds are described in the current
[backend contract](../../docs/backend-msx-neo8.md).

`make relational-boundary-msx` executes 256 activations on both the host and Z80
with persistent scalar/aggregate state, external sinks, lazy initializers and
an automatic helper frame. It produces a fresh inspectable SDCC/megalinker
project and fingerprinted evidence.

## Logical types and physical storage

Integer promotion, implicit widening, checked exact-integer conversion and
explicit wrapping casts are ordinary clauses in `source_rules.cpp` and
`source_signed.cpp`. Signed and unsigned typed operands require an explicit cast
between families. The kernel
gained a generic nonnegative remainder constraint for finite-width arithmetic.
It has no knowledge of Vexel's `UInt` or `Runtime` constructors.
Exact operands must fit before entering a concrete operation: `(#u8)1+300`
is rejected, whereas `(#u8)255+1` wraps to zero. A result annotation cannot
retroactively choose the arithmetic width.

A known value is an arbitrary-precision integer. A known runtime value is a
residual expression with a proved interval. Addition, subtraction and
nonnegative multiplication propagate intervals; possible unsigned wrapping
conservatively gives the full type range. C projection preserves logical
operation widths independently of storage widths.

Signed source values are mathematical integers, including negative values.
Wrapping, arithmetic right shifts and division truncated toward zero are
explicit relationships. Absolute value of the minimum signed value wraps back
to that value. Native signed parameters and storage use `int8_t`, `int16_t` or
`int32_t`; non-native widths are checked at boundaries. Caller-owned result
slots and `vx_NAME_read` carry a sign-extended two's-complement `uint32_t` for a
signed leaf. External result providers must use that same representation;
for example an `#i8` result of minus one is `0xffffffff`, not `0xff`.
Physical output selection proves the signed range fits before storing it,
then reading sign-extends from the selected storage width.

`make relational-signed-msx` checks 1,024 host and 773 Z80 activations against
independent integer oracles, including signed division, shifts, persistent
storage, materialized calls and the 32-bit minimum value. Focused host tests also
cover invalid external results and discarded division/shift traps under every
search ordering.

`source_numeric.cpp` supplies fixed-point and IEEE relationships. Fixed values
are signed or unsigned scaled integers; float values retain their binary IEEE
representation. Casts carry both types, so converting a fixed value rescales it
and converting a float interprets its IEEE bits. Explicit float-to-integer
casts reject nonfinite constants and retain runtime trap 6 even when discarded.
Native C boundaries carry the raw scaled integer or IEEE bits, not host C
floating-point values. The generated source plan records which banked numeric
helpers the MSX project needs.

Constant integer widths are admitted through a 65,536-bit construction budget.
Fixed constants share that width budget; floating constants support binary16,
binary32 and binary64. Current native runtime interfaces support widths through
32 bits. A wider runtime computation retains a target representation obligation;
it cannot silently truncate to the available C scalar. The numeric source tests
cover rounding, subnormals, signed zeros, NaNs, finite numeric choices, captures,
storage and independent IEEE arithmetic oracles. `make relational-numeric-msx`
checks 536 host and 532 valid Z80 activations plus a nonfinite conversion trap.

Source observations include logical result types and residual computations.
Physical choices select encodings and offsets; they cannot change a type query.
The demonstration explicitly prefers 16-bit storage, followed by 8-bit and
32-bit candidates, then minimizes extent and offsets. This is an illustrative
policy, not a measured performance claim.

| Contract with six payload bytes | Logical data type | Physical storage |
|---|---|---|
| Serial production and consumption | `#u16[3]` for each result | Shared 16-bit slots |
| Outputs may remain live together | `#u16[3]` for each result | Separate 8-bit slots |
| First producer returns, but its sink retains the output across the second | `#u16[3]` for each result | Separate 8-bit slots |

Known scalar fields can be embedded as constants and occupy no payload RAM.
Runtime leaves share an encoding per output. Source values, intervals, counts,
byte extents and placement remain in one request during search. The source
model is not solved first and handed to a separate allocator.
For this byte-aligned output arena, a closed lifetime proof can eliminate
equivalent placements. Pairwise exclusive results share offset zero; pairwise
simultaneously live results use contiguous intervals in source order. Empty
results require no interval. Both constructions have minimum extent for their
selected widths. Mixed or unclosed lifetime graphs retain the general placement
constraints; no unknown relationship is treated as exclusion.

## External boundary facts

An embedding adapter can provide a complete named resource footprint with
`--resource-read SYMBOL:RESOURCE` and `--resource-write SYMBOL:RESOURCE`.
Without such a footprint, the sink retains the conservative `external-world`
resource, which may alias any named external resource. Read/read access can
coexist; a shared write requires execution ordering. These facts do not turn an
external observation into a pure or compile-time computation.

`--sink-no-feedback SYMBOL` independently promises that a synchronous sink does
not activate another source before returning. It can close the activation
premise for static local storage. The default retains unknown feedback.
The sink ABI borrows its result buffer for the duration of the call and passes
value arguments as scalar leaves, including nested records, arrays and tuples.
It does not permit retaining the caller's result-buffer pointer after return.

An adapter may also offer an equivalent native statement for an execution-region
boundary using `--boundary-expansion SYMBOL:MACRO` or
`[[c_expansion(MACRO)]]` on the external declaration. This is availability of a
physical implementation, independent of the resource and feedback contracts.
The default `prefer(region_boundary_expansion,1)` selects offered statements;
zero retains wrapper calls. A scoped `prefer("boundary_expansion.SYMBOL",0)`
disables one offer. Multiple preferences combine by minimum. Different offers
for the same symbol are an error, independent of contribution order.

Define the macro before including `vx_bind_IDENTITY.h`. It receives one stable
structure lvalue with typed `a0...` argument fields and `r0...` result fields:

```c
#define ENGINE_READ(q) do { (q).r0 = engine_values[(q).a0]; } while (0)
#include "vx_bind_scan.h"
#if !VX_BIND_scan_EXPANDED_read
VX_BIND_scan_BOUNDARY_read { ENGINE_READ(VX_BIND_scan_DATA_read); }
#endif
```

The statement must implement exactly the same operation as the wrapper: same
results, observations, effects and callback behavior; initialize every result
field, preserve argument fields, retain no frame address, and return normally
to generated control flow. Fixed and automatic frames use the same interface.
The adapter owns native layout, address-space access and mapper restoration.
Offering a macro does not establish residency, purity, non-reentrancy, or permission
to move/collapse reads. A banked operation still needs its appropriate trampoline.
As with any external implementation, equivalence is an embedding obligation,
not a proof inferred by parsing arbitrary C.

Emission preserves the original publication, call, validation and snapshot
sequence. Missing selected macros fail C compilation explicitly. Availability
appears in `boundary_contracts[].c_expansion`; selected alternatives appear in
each binding's `codegen.region_expansions`. Ordinary source interfaces retain
their existing external ABI. On Z80, expanded boundaries with multiple result
leaves retain a generated function boundary to prevent SDCC's observed
aggregate-result forwarding error. These appear in
`codegen.region_expansion_barriers`; host C still expands them directly.
Scalar native queries retain direct expansion on both targets.
This layer makes alternatives inspectable and
selectable for measurement; it does not yet predict their cycle/ROM costs or
solve arbitrary native layout and bank-placement constraints.

`--access-order FIRST:SECOND` requires the first source's execution to complete
before the second starts. It does not release the first output. It can refine
an overlap contract; transitive edges participate in resource checks, and
cyclic requirements are contradictory. All these assumptions appear in
`source-plan.json` and the packaged target plan. The embedding adapter must
honor them.

`--occurrence NAME:SOURCE` supplies a separate boundary occurrence of a source
definition. `--source SOURCE` keeps its existing name as an implicit occurrence.
Each occurrence has its own invocation, C entry/read functions and result
lifetime; two occurrences of one source may receive independent arguments.
This also permits a sink callback to enter a second occurrence while the first
is suspended. Unknown sink feedback still prevents static locals in its call
ancestors; naming events alone does not close that feedback contract.

Physical boundary names must avoid runtime C symbols, another boundary's reader
name, and the generated prefixes `flow_`, `ensure_`, `load_`, `store_`,
`invoke_external_` and `external_`. These are adapter restrictions, not Vexel
identifier restrictions: `--occurrence publicName:ensure_1` exports a source
named `ensure_1` under a safe physical name. Conflicts are diagnosed before
creating the output directory.

`--mode graph` supplies a finite occurrence contract with no implicit ordering
between occurrences. `--event-order FIRST.EVENT:SECOND.EVENT` adds a precedence
edge, using `begin`, `produce` or `release`. Every occurrence already orders its
begin before production, and production before release. For example:

```sh
build/relational/vexel-source example.vx \
  --occurrence first:entry --occurrence second:entry --mode graph \
  --event-order first.release:second.begin --emit-c generated
```

This contract permits result-buffer reuse. Changing `first.release` to
`first.produce` orders execution while retaining distinct result storage.
Unordered lifetimes may overlap, including two occurrences of the same source.
Mixed graphs reuse only intervals whose non-overlap follows from the supplied
edges. Shared persistent/external writes still require execution ordering;
release ordering and resource ordering use the same event graph. Cycles are
contradictory, and invalid occurrence/event names are rejected.

The adapter owns each occurrence: it may have at most one activation and one
outstanding result under that name. One finite epoch must finish and release
its results before the adapter reuses its names. Releases are supplied facts,
not implicit consequences of returning from C or reading one result leaf.
There is no language pointer or thread primitive. The report and exported
project record source-definition identity, occurrence identity, events and
ordering. The actual call graph propagates each function's originating source
occurrences. A function can use exclusive locals when no admitted overlapping
pair can reach it, provided recursion and open feedback are also excluded.
Thus private wrappers and disjoint helper paths can use their own static locals
while a helper shared by overlapping callers keeps automatic storage. Ordered
callers can share that helper's locals. The report records each function's
source occurrences and overlap result; this is not a stack-bound proof.
Lazy initializer bodies also participate in that graph. Both loads and first
stores can call an initializer, whose transitive callees inherit the accessing
source's ownership. Initializer cycles therefore remain visible to recursion
checks. Resident initializer temporaries retain automatic storage, matching
their emitter, and an empty owner set is insufficient evidence for exclusive
storage. This prevents a helper used through initialization from appearing
private to another source that calls it directly.
The older overlap profile retains its conservative automatic policy.
`make relational-occurrences-msx` checks retained results, released reuse,
mixed lifetimes and nested callbacks on host and Z80.

Feedback occurrences are explicit contracts. `[[stream(capacity)]]` provides a
checked ownership pool for repeated serial execution and retained results;
capacity is ordinary source data. It does not infer arbitrary concurrency.

## Output boundary and target project

Source parameters may have fixed nested array, record or tuple interfaces.
Ordinary annotation relationships establish their layouts. The source plan
lists each logical parameter and its flattened native leaf arguments, in
field/element order. Empty aggregates take no arguments. Range and record
invariant checks precede source-body execution, including for unused inputs.
A partial type query does not require the full physical input interface.

For each requested source, `vx_NAME(...)` produces its result and
`vx_NAME_read(index)` exposes scalar leaves in the reported order. The sink must
finish reading before its contract permits storage reuse. These are C adapter
interfaces; Vexel retains its record/array result and gains no pointers.

The model describes one live result per listed occurrence.
Output storage is derived from returned structures; the boundary contract
supplies ordering and release relationships. Explicit occurrences can overlap
for the same source; arbitrary event streams and ownership pools are not inferred. The execution checks
produce and consume each result within that finite contract. They introduce
neither threads nor interrupt semantics.

The projection emits individual banked source/function modules and resident
wrappers that restore the caller's mapping. NEO-8 startup, runtime, SDCC
settings, megalinker and RAM/ROM checks use the existing target assets.
The payload budget covers output storage only; the project checker measures
the complete linked resident footprint. The 1024-byte stack reserve is a budget,
not a proof of worst-case use.

Reports fingerprint every parsed source input. Packaging refuses inputs changed
after resolution and records SHA-256 provenance for source inputs, executable,
C projection and plan. Hand edits remain possible and require fresh semantic
validation.

Resource expressions provide binary-safe strings and sorted directory tuples.
Their file contents, directory membership and path-selection facts also enter
the input report and packaging checks. A type query does not read file contents;
a directory shape query records the listing without loading each entry. Unknown
metadata remains verbatim in `source_metadata`.

Process imports require `--allow-processes` when their output is demanded.
`--process-timeout-ms` and `--max-file-bytes` bound execution and output. Each
command and working-directory pair has one recorded result per input revision,
including across `--then` requests. Type-only queries never execute commands.
Process output is recorded as an input; the shell command's own external
dependencies are not automatically discovered. A new resolver invocation
executes demanded commands again. `make relational-resources-msx` checks file
bytes and directory-derived constants in the generated host and Z80 code.

Capability queries (`?function(args)`, `receiver.?method(args)` and
`receiver.?member`) use closed declaration membership and signature matching.
They do not execute arguments or validate the chosen body. Use an ordinary
conditional to select an implementation; the selected body keeps all ordinary
errors, effects and constraints. Unknown and ambiguous signatures do not mean
absence. `#{ ... }` is rejected with a migration diagnostic. The transaction
implementation, typed absence completions and fallible annotation traversal
have been removed.

Declaration tables are immutable per lexical scope and input revision. Source
recognition loads the statically named import graph, including cycles, before
semantic requests. Imported members are collected for their owning lexical
scope. Body expansion cannot add caller overloads. Runtime values and type
promises remain independent of declaration membership; a new SourceProgram is
a new source input revision.

Control proofs share finite native bits across arithmetic and comparisons.
Addition, subtraction, multiplication, bitwise operations, proved-valid shifts,
casts and signed/unsigned ordering retain their wrapping widths. Thus `n < 128`
can imply `n < 129`, and `n - n` can be proved zero from its operand identity.
The proof module is source-specific; the relational core still treats IR terms
as uninterpreted constructors. Numeric atoms retain their original expression
so loop initialization can forget changing dependencies at each back edge.

Division and remainder use a finite word relation when the denominator's bits
prove it cannot be zero. Signed division truncates toward zero, signed remainder
follows the numerator, and the minimum value divided by minus one wraps at the
declared width. Variable shifts use a barrel relation when the complete count
is proved nonnegative and smaller than the width. Bounds and ordinary operand
relationships can supply that evidence: `n|1` is nonzero and `n&7` is a valid
byte shift count. If validity is not established, the result stays opaque to
this proof; no input is silently excluded. Original evaluation and trap
obligations remain in the execution graph. The same word relations support
ordinary dead-branch reasoning and initialization through runtime loops.

An opaque call result remains an overapproximation. A failure to prove a
branch impossible does not permit ignoring its obligations. Control reasoning
can use sufficient first-iteration loop facts, but it does not prove general
termination or arbitrary native-call equivalence. An optional simplification
may retain the original expression if it reaches its proof budget.

Native calls now have a separate sufficient result certificate. A private body execution uses the actual,
grounded argument and capture facts; it must close the result type, value,
state, returned captures and execution graph. Capture types, incoming values
and returned writes retain the ordinary interface's distinct access modes.
Output-only and type-only captures cannot borrow a lexical initializer or a
caller value that the physical interface does not supply. Returned capture
values require definite initialization and map to the physical result slots,
including the reserved dummy return slot for a static or `NoValue` result.
Receivers keep their logical roots and paths designated at the actual call
site; shared and overlapping views therefore preserve aliases. Later argument
side effects cannot reselect a path. The private receiver has a fresh type view
and reads/writes the supplied root state, including temporary receiver roots. Finite source choices and joint type choices remain
caller obligations. An unsupplied older value cannot become a fresh private
choice. A shadow context prevents recursively expanding native calls inside
this certificate, and a distinct proof identity isolates any native frames it
constructs. The optional attempt has a separate 20,000-work cap; failure leaves
the original physical expression available to ordinary resolution.

The result is input to numeric reasoning only. It cannot supply emitted call
temporaries or commit the private state or graph. A proved constant or guard
may simplify the caller, while the original call and its stores and traps stay
in the execution graph. This supports identity/zero results, tuple leaves,
read-only, output-only and changing captures, aliased receiver views, and
recursive base cases selected by a caller guard. Certificates compose through actual earlier call results;
a cycle retains its opaque slot. Direct expression parameters remain inline
under the RFC. This certificate does not provide a general summary for private
nested calls, arbitrary recursive results or captured expression thunks. Distinct stateful calls retain distinct values.

A native interface's sufficient universal proof remains separate from fallback
to an actual invocation. The fallback preserves the actual runtime expression
and its calling context. Replacing it with a generic signature input after the
universal proof failed could lose the fact that makes the actual query valid.

Overload matching checks only the needed parts of a signature. An excluded
outer category or earlier parameter does not demand an irrelevant annotation
tail. A needed unavailable annotation remains an obligation and cannot cause
fallback to another overload. `make relational-capabilities-msx` checks guarded
calls and explicit recursive state on the host and Z80.

For pairwise exclusive output lifetimes, the backend can prove the preferred
feasible encoding independently for each output. It keeps the existing policy
(16 bits, then 8, then 32), all source constraints, and all physical constraints.
This removes equivalent encoding combinations in the current byte-aligned arena,
whose outputs have no fixed addresses. Coupled lifetimes retain joint search.
New alignment, address or resource coupling must be included in this proof before
using it with a broader allocator. Encoding preference cannot resolve source
ambiguity.

Builtin sorted iteration uses stable ordering and ordinary `<` methods.
Comparisons can modify their private receiver copy, but cannot write enclosing
storage or require runtime execution. Runtime payload fields are allowed when
the comparison result is known. `make relational-sorted-msx` checks stable
ordering, preservation of the original array and caller completion.

## Coverage and limits

Tests cover seedless dimensions, declaration order, generic specialization,
nominal identity, separate type/length/value captures, partial and later complete
demands, schema conflicts, arithmetic, conversion/indexing failures, closure,
exhaustion and physical revision. They permute FIFO/LIFO/random schedules and
forward/reverse branches. Generated host C checks wrapping and logical-width
preservation for all 256 unsigned-byte inputs.
The standalone source oracle checks 546 further cases: 18 complete
declaration/search permutations and 528 linear-shape cases.
The control oracle checks another 18 declaration/search permutations and all
256 byte inputs for each generated C projection (4,608 host executions).
The execution suite checks ordered argument effects, captured-state recursion,
state-dependent result types, branch joins and completion independent of values.
Five runtime-call programs run under all six worklist/branch schedules and all
256 byte inputs (7,680 host executions), including mutual recursion and repeated
calls sharing a capture. Instrumented C verifies that a call returning a
discarded value still executes each recursive activation exactly once.

The MSX driver checks three projects against an independent integer oracle:
753 host input pairs and eight Z80 pairs each. Deliberately aliasing overlapping
outputs produces a wrong result, retained in `comparison.json`.

The completion work now extends this profile with nested aggregate execution,
logical member/index places, output-only captures and recursive aggregate call
interfaces. Structural/dependent overloads and candidate-local dynamic schema
capture discovery use the same property relationships. Unrolled iteration,
sorted scalar iteration, unrolled repeat and explicit runtime loops carry
normal completion separately from return, break and continue. Runtime loops
use parallel value updates instead of recursive C calls. Their initialization
relation is separate from their physical storage: a finite Boolean fixed point
follows first entry, predicate continues, body back edges and exits, and checks
each actual read. A nonempty fixed iterable guarantees its first body entry.
Unchanged reaching values retain their identities across the loop. Tests of
changing values are forgotten at back edges unless a separate invariant proves
them stable. Branch and short-circuit conditions are premises of their reads.
Nested loops pass their safety obligations to the enclosing loop's proof.

This supports first writes in loop bodies and predicates, conditionally
initialized values read under matching guards, output-only helper captures,
aggregate storage and exits that bypass initialization. Untouched declarations
do not acquire a type obligation merely by being visible. The C projection uses
parallel readiness and value updates; unused storage bytes never establish a
source value. Each reaching location carries its sufficient initialization proof
and its executable readiness separately. A completed loop exposes its actual
readiness flag to following execution; branch joins and capture
boundaries retain both facts. Boolean proof normalization may reorder partial
operations, so a proof formula must never become an emitted readiness value.
The physical flag follows the original guarded assignments, including nested
loops and short-circuit division, shifts and indexed access. The original
operations and their traps remain in the execution graph.

When the readiness proof is insufficient, an optional refinement tracks native
numeric words through the same least fixed point. Actual entry, predicate and
body values supply transition equations; the proof follows only words needed
by guards/readiness and the values those updates depend on. Fresh next-state
bits are projected and renamed together with readiness. Local call results and
untracked values remain an overapproximation at each edge. The iterable cursor
has its actual initial value and increment. Signed wrapping and overlapping
bit identities use the existing finite-word algebra.

This closes counted first/later writes, reads justified by earlier iterations,
count-dependent exit guards, break/continue edges, nested loops and native
captured writes. Result-type proofs can use the relation without evaluating an
unknown or cyclic argument value. The refinement has the control proof's
100,000-node bound; exhaustion preserves the preceding sufficient proof. It
cannot publish an unfinished fixed point as an invariant. Initialization is
still separate from termination and general loop-result equivalence.
Unsupported arithmetic and larger relations can remain
unresolved. `make relational-loop-init-msx` checks the programs with automatic
and exclusive local storage against independent host and Z80 oracles.

Finite `?{...}`, `?(#T, domain)` and `?(predicate)` regions now contribute
ordinary finite domains and arithmetic constraints. Ranges and integer division
and remainder support the unchanged `examples/sudoku.vx`. The independent
Sudoku oracle checks complete observations across declaration and search-order
permutations. Domain restrictions, clause transactions and dependency wakeups
belong to the generic core; it has no Sudoku operation. The source CLI's work
budget defaults to ten million and is adjustable with `--max-work`;
`--max-nodes` bounds source relationship construction separately.
The full Sudoku example and its oracle use a twenty-million-step budget, with
guessing disabled in the oracle. Control refinement preserves partial integer
relationships and refines aggregates leaf by leaf, so an indexed constraint
does not wait for the entire array to become concrete.

The finite RFC-family acceptance boundary is in
[the closure checklist](../../docs/closure-checklist.md); verified checkpoints
are in [the completion ledger](../../docs/relational-completion.md).
General schema dependency and numeric loop-invariant proving can remain open
under the declared solver budgets. Signed, fixed-point and IEEE numeric families,
canonical representation casts, tuples, receiver and operator methods,
broadcasting, iteration methods, signature probes, scoped imports, resources and bundled math
now have executable relationships. Expression parameters and control transfers across
operand evaluation pass their integration tests. Exact mutable values
without a concrete runtime representation cannot cross a loop back edge.
Unsupported invariants remain explicit obligations rather than guessed facts.

Loop conditions consume their own break and continue transfers. Continue in a
condition preserves its preceding updates and restarts the condition; it never
requires a Boolean from the transferred path. Iterable evaluation precedes the
new loop's control scope and retains existing lexical targets. An expression
argument's annotation is applied only if that evaluation completes normally.
The expanded completion oracle checks these interactions on the host and Z80.

The SDCC projection preserves shared residual operands with volatile automatic
roots and snapshot reads. These are realization constraints, with a code/stack
cost, rather than Vexel boundary effects. The reason and minimal C reproducer
are recorded in [sdcc-operands.md](../../docs/sdcc-operands.md).

Closed total integer sources also receive optional typed C value headers.
These share repeated calculations and specialize constant arithmetic without
the arena serialization/read interface. A result guard can expose
separate input dependencies for its predicate and selected case. Eligibility
checks include execution completion, not just returned values; effects,
unsupported operations and potentially trapping discarded expressions retain
the ordinary projection. Input domains and fixed-width semantics are unchanged.
The case interface also admits invariant results, independent selections and a
returned predicate. The first selected result leaf provides the partition;
existing common-guard interfaces keep their established predicate. A client
uses the generated branch inspector, rather than assuming a source expression
defines that partition. It specializes all result leaves under the chosen guard
before collecting input dependencies. Boolean consequences such as both false
operands of a false disjunction can eliminate further inputs. Case facts never
escape into the full interface or another invocation, and their conversions
remain explicit. If the optional case construction exhausts its budget, the
certified full interface remains available. `test_case_facts.py --msx` checks
independent word-boundary oracles, untouched outputs, conjunction/disjunction
and negation, automatic/fixed bindings, and cases requiring no input fields.
`test_case_partitions.py --msx` additionally checks independent guards and
numeric inputs whose nonzero test must not replace their original value by one.
Within factored guards, invariant leaves retain their result slots and are
written once; stable inputs are not copied again into redundant temporaries.
If frame planning proves exclusive activation ownership and inputs have full
native domains, `vx_fixed_NAME.h` offers named static storage for inputs,
temporaries and results. Its `fixed_frame` report gives the target byte count;
the embedding selects and budgets this alternative separately from the ordinary
arena frame plan. The live interval includes preparing inputs and consuming
results. All assignments remain per-invocation, and callbacks must occur outside
that interval. The automatic value interface remains available for other uses.
The [value interface contract](../../docs/backend-msx-neo8.md#pure-value-interface)
describes ownership, supported operations, case selection and physical limits.

Generated value and region headers select the runtime support functions used by
their emitted code. Independently guarded `vx_support_value_*` components compose
across headers without emitting every unused SDCC helper. Handwritten C retains
the full `vx_value_runtime.h` interface. The standalone fixed alternative also
keeps a real function boundary after publication; expanding it into its caller
can reintroduce SDCC operand forwarding despite volatile input fields.

Products in a value or region realization use the result's modular width:
byte/word products need only byte/word arithmetic support, while general dword
products retain the existing runtime. Constant powers of two and their modular
negations use one unsigned shift and optional negation, then restore the
original result type. The optimization changes neither overflow semantics nor
input domains, and does not duplicate operand evaluation.

`[[c_binding(identity,entry,frequency,contract)]]` on a defined source requests
a named embedding interface. `entry` selects the full value graph, a
certified `case0`/`case1`, or an ordered `region`; `contract` is `exclusive` or `open`. Configuration
resolves the frequency expression alongside ordinary obligations. After value
and activation proofs close, the projection selects eligible frames under
`value_frame_bytes` in frequency/size/identity order. A budget miss or open
ownership retains automatic storage through the same client macros. The
`value_bindings` report explains each decision; `vx_binding_frames.c` owns the
fixed allocations once. An embedding can cap an estimated frequency with
`--prefer binding_frequency.IDENTITY=N` or
`[[prefer("binding_frequency.IDENTITY",N)]]`. Caps range from zero to one million
and combine by their minimum with the binding's declared frequency. A zero cap
selects automatic storage even if fixed storage would fit; the interface remains
callable with unchanged semantics. This lets native build options describe an
unused alternative without assigning it fixed RAM. It is a placement hint,
not a lifetime proof or dead-code permission. Unknown identities and other
configuration roles are rejected; source and CLI contribution order is immaterial.
`[[c_disjoint(a,b,...)]]` on a module declaration
states pairwise disjoint full input-through-result intervals for named
bindings. It is order independent and nontransitive. Each selected slot must
exclude every pair of its members; candidates use the least incremental RAM
cost, with deterministic slot ties. Typed C unions share the storage and
target `sizeof` assertions check the budget. `static_bytes` reports physical
slots; `unshared_static_bytes` counts their logical members. Unknown bindings,
self exclusions and fewer than two identities are rejected. The facts cannot
promote open or otherwise ineligible bindings. This planner is separate from
ordinary arena local planning and does not infer C engine lifetime facts.

Pure value realization can also synthesize immutable ROM tables from finite
rules. It evaluates a candidate over every bit pattern of one byte input,
including signed interpretation and each intermediate integer conversion.
Ignored low bits, periodic indices and modular narrowing of stored results
are accepted only after exhaustive comparison. A constant suffix may use a
bounds branch and a default value. The original result format is restored at
the load. The table belongs to the consuming function's translation unit;
no address escapes to the language or survives a bank transition.
Uniform results become immediate constants. Two adjacent constant runs become
a threshold comparison; a single exceptional run becomes an equality or
interval test. Periodicity and ignored bits apply to these rules too. These
realizations consume no table storage. Their certified constants, comparisons
and selections rejoin the ordinary value graph before branch specialization,
so callers can share decisions and specialize arithmetic across this boundary.
Aligned intervals become masked tests of the original byte, avoiding a shift
merely to bring a tested bit down to bit zero.
A uniform realization also drops its now-unused producer from scheduling.
An indexed sequence that exactly advances or retreats by one in the original
modular result width can also become native arithmetic, optionally under its
certified guard. Every entry is compared after restoring the result format;
storage narrowing is not a license to remove an intervening signed conversion.
Tables made unreachable by these replacements are neither emitted nor charged
to the reported ROM size. General finite functions keep their indexed form.

The finite interface may also be a computed byte. For each candidate consumer,
the planner intersects the intermediate values on every nonconstant dependency
path. A retained byte value is a sufficient input for the rest of that graph:
its producer stays executable, while all 256 possible bit patterns are evaluated
through the remaining operations. Thus `(x ^ y) * 7` can become a table indexed
by `x ^ y`, without incorrectly turning `x * y` into a table of only `x`.
Selected, narrowed and signed intermediates use the same proof. A computed index
uses a single lookup rather than an indexed suffix guard that would repeat its
producer. A native rule with immediate arms can use a computed input directly:
its comparison evaluates that producer once.

This is a bounded backend heuristic, not a semantic restriction or a claim of
optimal code. Direct-input discovery first considers subgraphs with at least
four selections. Intermediate discovery retains up to eight possible cuts per
node, visits at most 64 nodes per candidate, and compares estimated work with
ROM cost. The estimate counts only operations whose last use disappears: work
still needed by another output is not credited to the replacement. Selections
receive a larger estimate than simple arithmetic, and nontrivial multiplication
a larger estimate again. These weights are backend heuristics, not cycle proofs.
Additional cuts must remove strictly more estimated work than their native
rule or indexed load introduces. Rediscovering an already cheap comparison
does not justify changing its graph and demand order. The initial family of
large selection trees retains its existing bounded policy.
By default it enumerates at most 16 candidate cuts and spends at most
512 bytes across eight tables, with a 256-byte limit on each table. Mutable
effect regions keep their existing emission. Identical tables within a
realization share storage, including when the byte ceiling has already been
reached: reuse costs no additional bytes. The `codegen` report gives
`finite_tables`, `finite_table_bytes`, `finite_lookups`, `finite_constants` and
`finite_guards` and `finite_arithmetic`; actual SDCC section sizes and callback
measurements still decide whether the realization is useful. No source
annotation or hand-written C table is required.

`value_table_bytes` (0..2048, default 512) and `value_table_search` (0..64,
default 16) are ordinary realization preferences. Zero bytes disables all
tables; zero search disables only the additional intermediate search. Source
and CLI contributions combine by minimum. A quoted source key such as
`[[prefer("value_table_search.cursor",0)]]`, or CLI
`--prefer value_table_search.cursor=0`, caps a declared `c_binding` individually.
The effective allowance is the minimum of that cap and the global allowance.
Unknown binding names are rejected. This allows measured target tradeoffs to
select realizations without changing the game logic or weakening its proofs.

A finite interface also preserves the input on which its certificate depends.
When an established branch fact specializes that input, the same immutable table
can consume the specialized value. A constant index evaluates the certified
lookup directly, including suffix defaults and signed storage conversions.
Knowing a narrowed byte equals one still does not fix its original word.
Speculative branch expansion rolls back both newly created graph nodes and
their lookup identities when its cost limit rejects the candidate. This keeps
later node reuse from acquiring a rejected candidate's physical meaning.
Physical index shifts and masks also belong to the value graph. Different
lookups can share their stable index producer, and scheduling follows those
implementation dependencies even when a synthesized index was created after
its original consumer. If a native expansion exhausts the graph budget, its
graph changes and lookup identities roll back together to the certified lookup.

The native emitter also carries known literals through complete conversion
chains and established branch choices. Every narrowing and signed conversion
is applied before selecting an immediate operand. Wrapped-zero additions and
zero products omit their arithmetic helpers; other constant products can use
shifts or sparse arithmetic. This changes value work only. An ordered call
whose entire result becomes zero still executes, as checked by the automatic
and fixed-frame host/Z80 literal-operand oracle.

A shared conditional value can also be carried through its consumers as control
flow. Instead of storing `v = c ? a : b` and reading that temporary in several
outputs, the value emitter can branch on `c` and specialize the outputs to `a`
or `b`. This applies only to certified total integer graphs. Original widths
and signed conversions remain at the eliminated selection. Invariant results
are written once. Within each arm, comparisons with the same operands share
their ordering facts: `a < b` implies `a <= b`, and its false arm reduces
`a <= b` to equality. Same-width modular cancellation also permits
`x + (k - x)` to become `k`, including wrapping cases.

Different selected values can also share a decision. When an unconditionally
demanded Boolean guard controls at least two distinct selections used by wide
arithmetic, the emitter can specialize the outputs together. Coordinate updates
can therefore choose an elapsed-time
case once, then compute both coordinates on that path. Equality to a constant
also fixes the exact compared operand throughout that path, provided the constant
fits its representation. Knowing `(u8)wide == 1` fixes the narrowed value, not
the original `wide`; unsigned comparisons with negative operands remain
conservative. Invariant outputs are still written once.

Stable pure temporaries produced while writing those invariant outputs remain
available to the following branches. The emitter returns only names declared
in the enclosing C scope; a temporary declared inside one alternative never
escapes that alternative. This avoids recalculating a shared producer after a
decision split, including producers used as finite-table indices. Mutable
regions retain their existing scheduling and lifetime rules.

Each eligible selection needs at least one small alternative without a nested
decision (at most eight nodes). Comparison-only consumers and selections with
two complex alternatives do not qualify: measured ball and melee callbacks did
not benefit from expanding them. This is a target cost policy, separate from
the pure-graph equivalence proof.

These rewrites use only decisions already demanded unconditionally in the
current block. Discovery stops at inactive selection and short-circuit arms,
materialized values and table lookups. It does not cross an effect boundary or
alter the public case-selection API. Mutable regions retain their existing
schedule. `value_choice_branches` is a realization preference from zero to eight,
with a default of two combined value/predicate splits per realization. Zero
disables this strategy; larger allowances spend ROM to specialize more consumers. Source
`[[prefer(value_choice_branches,8)]]` and command-line
`--prefer value_choice_branches=8` use the same configuration equations. Multiple
allowances combine by their minimum. The heuristic retains small arms and at
most 64 added graph nodes per split. `shared_select_branches` and
`predicate_branches` in the binding `codegen` report record the two kinds of
split; their sum cannot exceed the allowance. Code-size and timing
checks remain necessary: reducing temporary storage can duplicate instructions.
The shared-value cost rule requires a dynamic addition, byte subtraction or
byte-ordering consumer whose stored operand would need an SDCC isolation helper.
Merely removing a selected value
used in constant comparisons did not justify the measured code/stack cost.
After substitution, representation bounds also prove constant comparisons such
as `u8 < 0`, and remove their unused alternatives. Unsigned comparisons involving
negative operands retain their original lowering; signed comparisons use their
explicit signed operand formats. A larger allowance does not relax semantic
checks, module size limits or external compiler resource limits, nor promise
that more splits are always faster.

Region entries include integer calculations, structured loops and synchronous
external calls. Boundaries may have no arguments or no results; an empty
publication set still participates in the ordered effect flow. Typed boundary
macros preserve one adapter across fixed and
automatic frames. The exclusive interval includes callbacks and continuation
state; callbacks may use other, separately allocated bindings. They must not
reenter the region or reuse its workspace. This scoped contract does not claim
`no_feedback`. The region projection rejects unsupported traces explicitly.
Readiness invariants, control-flow facts, dead-transfer elimination and
interference-based cell reuse precede parallel-copy scheduling. Repeated
decisions are bypassed only while their facts remain valid; old values survive
when a later decision still needs them. Input cells belong to the occurrence
after publication and may be reused once dead. Final results are available
after the run. The report includes coalesced and entry-initialization bytes;
`--prefer region_copy_schedule=0` selects buffered transfers for comparison.
`test_region_projection.py --msx` checks both realizations against independent
loop/callback oracles, including another live binding inside a boundary.

Region cell reuse also follows publication lifetimes. Every boundary call
defines its result fields. A call-site snapshot can use those fields directly
when its value does not have to survive another definition. When a later call
can overwrite a still-needed result, interference retains a separate snapshot.
Copies establish equality: two live names may share a cell while they denote
the same value, until a redefinition makes them conflict. Entry inputs are
independent; parallel destinations conflict, and published ABI fields keep
their names. This can eliminate the copy from a loop counter to its boundary
argument without losing the counter across the callback. No engine read is
cached or moved across a call by this reuse.

Complete publication objects can also share storage after their last use.
`prefer(region_boundary_overlay,1)` is the default: share compatible objects,
but keep directly expanded native statements in dedicated storage to limit
SDCC register pressure. Aggregate expansions retain a target function boundary
and may share. Zero keeps all objects separate; two permits every compatible
object to share, including direct expansions. These are physical preferences,
not lifetime facts, and combine by minimum. The planner recomputes liveness
after scalar coalescing, so a
loop counter occupying an argument field extends that object's lifetime.
Arguments/results used by the same transfer conflict even when individual
scalar lifetimes appear disjoint: reversed multi-field handoffs must not
overwrite a later source. Retained replies and synchronous callbacks are
included. Empty boundaries still have an ordered call and one byte of C storage.
Compatible objects occupy typed union members in a deterministic size-first
allocation. The report's `region_boundary_slots`, `region_boundary_conflicts`,
`region_boundary_bytes` and `region_boundary_unshared_bytes` expose the decision.
`region_boundary_dedicated` lists objects kept separate by the cost preference;
these are not reported as lifetime conflicts.
This heuristic does not claim optimal packing or reduced hardware stack usage.

Both native wrappers and expanded statements borrow only the current typed
object during the call. They must initialize every result and preserve argument
fields; they cannot retain its address, rely on address identity, or read a
previous invocation's results. Adapters use the generated `_DATA_SYMBOL` macro
because physical member paths may change. Results needed by the continuation
remain live after return; later calls cannot overwrite them until their last use.
`test_boundary_expansion.py --msx` exercises dead-object reuse, reversed aggregate
handoffs, retained replies, conditional callbacks and automatic/fixed frames,
with sharing and native expansion independently disabled and enabled.

Decisions that consume only truth are lowered to control-flow edges before
the final liveness analysis. Pure selections, negation, conjunction and tests
against zero can choose successors directly. Numeric results retain their
values, selection-arm conversions retain their widths, and narrowing such as
`(#u8)256` still produces false. Inactive arms stay inactive. Exploration is
bounded to 64 nodes per original decision and stops expanding at 4,096 flow
positions; remaining expressions use the existing predicate emitter.
`region_direct_decisions` counts rewritten decisions, while
`region_coalesced_bytes` includes reused call snapshots and copied loop cells.
Callback order, fresh boundary reads, volatile operands and arithmetic isolation
helpers are preserved. The region oracle covers conditional overwrites, live
old/new values, numeric truth and narrowing on both host and Z80, using fixed,
buffered and automatic realizations.

`prefer(region_predicate_functions,1)` also offers small pure predicate functions
that borrow the current frame. Their call boundary separates previous stores
from the calculation, so selected byte operations can use immutable input
lowering without passing every operand through a generic arithmetic helper.
Fixed frames need no arguments; automatic frames pass their own stable frame
pointer. Calls remain at the original decisions/checks, read current values
on every invocation, and introduce no persistent state or cached native reads.
Identical predicates in one binding share a function, not a computed result.

The initial family admits at most two byte input fields, one dynamic byte
addition/subtraction or unsigned ordering operation, and surrounding byte
casts, constant operations and comparisons. It rejects wide operands, selected
expressions and shared computed intermediates. A binding emits at most sixteen
distinct predicate functions, each with at most twenty-four graph nodes.
Remaining conditions retain their existing lowering and operand safeguards.
`region_predicate_functions` and `region_predicate_calls` in the codegen report
count definitions and static call sites. Preference zero retains the generic
helper realization. This is a measured backend alternative, not a claim that
all function extraction reduces stack use or execution time.

`test_region_predicates.py --msx` compares both policies and storage choices
against an independent oracle, including byte wrapping, signed truth, word
carries, conditional demand and changed replies on later loop iterations.

The C projection and generated MSX projects now exercise aggregates and runtime
loops as well as scalar recursion. The host projection is `vx_bridges.c`.
The target project selects `vx_banked.c` plus individual `vxp_*.c` modules for
source entries, materialized functions and power helpers. Resident wrappers
restore the caller's mapper state; C or assembly files in `user/` override a
matching generated module. `make relational-banks-msx` verifies both override
forms on Z80, and `make relational-math-msx` exercises all numeric helper banks.
Call frames retain automatic ABI parameters. Local buffers follow the reported
per-function policy; a reserved stack budget is not a whole-program bound.
Owned mapper storage, control-preserving bank continuations, measured bank-cut
feedback and assembly stack analysis are implemented by the current
[backend contract](../../docs/backend-msx-neo8.md). `make closure-msx` exercises
their interactions; `make msx-project SOURCE=... OUTPUT=...` exports a project.
Supply `VEXEL_OPTIONS='--user-directory path/to/embedding'` when C/assembly
adapters or stack contracts must participate in the first measured build.
Those files populate `user/` on every partition retry. Their content hashes
join the attempt's input identity, so an edited binding cannot reuse an earlier
rejection or placement measurement.
No fallback to the established Vexel solver is linked into this executable.

## Calculation scheduling

The ordinary C path now uses the same typed shared-node scheduler as value and
region projections. Repeated total integer calculations receive sequenced
scratch assignments within their original expression. Lazy arms, effects and
runtime checks remain boundaries; statement-local scratch has no validity
across a subsequent call or loop transfer. Scratch lanes participate in frame
budgets and bank continuations before C is emitted.

Within an interval, the shared scheduler releases a retained value after its
last complete consumer and reuses that slot for a later value of the same type.
Future branch consumers pin their producers; inherited locals and separately
evaluated C arguments retain separate ownership. Wide byte copies finish before
operand slots can be recycled. This applies equally to ordinary C, value frames
and region expression scratch, without changing source evaluation order.

`frame_planning.functions` reports `calculation_scratch_bytes`,
`calculation_intervals` and `retained_calculations`. A fixed frame requires the
existing exclusive activation proof and enough budget; otherwise its scratch
is automatic. These sizes are not worst-case stack bounds. `make calculation-msx`
runs the host and Z80 checks for shared loops, traps, effectful snapshots,
recursive frames, lazy initializers and small bank continuations.

`--prefer value_scratch_reuse=0` assigns distinct slots to retained calculations
inside value and region intervals. The default is 1. Scope the preference to a
binding, for example `--prefer value_scratch_reuse.scene_camera=0`, to compare
one realization without changing unrelated bindings. Ordinary C intervals use
`calculation_scratch_reuse` and a physical-function scope such as
`calculation_scratch_reuse.vx_entry`. Unknown scopes and values outside 0..1 are
errors. Global and scoped preferences combine as allowances; neither the CLI nor
a backend may increase a source cap. Distinct slots do not disable reuse of
frame lanes between completed statements.

Both function and binding reports publish `scratch_policy`, the exact scoped
preference identity. Functions and binding `codegen` objects publish
`scratch_reuse` and `scratch_reuses`; the latter counts reuse decisions, not bytes
saved or target instructions. This lets a measured backend discover applicable
alternatives from the closed plan instead of guessing names from emitted C.

The calculation graph uses the same native integer range transfer as the
program's range analysis. A retained result can occupy a smaller signed or
unsigned scratch cell when its complete range fits. A pure operation can also
use a smaller width when both converted operands and its complete result fit
that width with unchanged signedness; shift counts must remain valid there.
Otherwise arithmetic retains the logical operation's width and intermediate
wrapping. This proof is separate from narrowing an unobserved high-bit residue.
Exact intervals can become constants after the operation's completion proof.
Result fields and ABI types remain unchanged. Wide byte-copy and equality shortcuts additionally
check the actual cell width. `narrowed_calculations` in function reports and
`narrowed_values` in binding `codegen` report the number of such retained values.
These counts describe emitted plans, not total RAM savings after overlaying.

Ordinary calculation inputs keep their closed value envelopes as well as their
physical types. The shared transport interface resolves compact result and
loop indices back to logical leaves and converts the established bounds to
their actual carriers. Thus a private divisor supplied only in `1..16` can
select native byte division even if its declared type is `#u32`. Every caller
and loop backedge contributes; a possible zero retains the checked operation.
Public input validation, foreign reply checks and invocation snapshots remain
in place. The transport-domain tests cover formal arguments, captures, compact
results, loop state, wrapping and excluded source inputs on host and Z80.
`examples/transport_domains.vx` shows these facts crossing several boundaries.

Finite table search fills unreachable byte patterns with a defined value from
the admitted envelope. It never evaluates a zero divisor excluded by a retained
entry check. If a candidate cannot be evaluated, that optional cut is rejected
and the native implementation remains available.

`narrowed_operations` counts whole-value reductions and reductions justified by
every consumer needing only a shared producer's low native byte/word. The
physical graph joins these demands before reducing modular arithmetic, casts
or selection arms. A wide consumer prevents residue-based reduction. Right
shifts, division, comparisons and truth tests retain the operand bits they need.
No check, source effect or wrap boundary is removed by this analysis. Mutable
region transfers are excluded from operation-width reduction.

`value_output_schedule` permits output-demand scheduling (0/1, default 1),
with a `value_output_schedule.BINDING` scope. The global and scoped allowances
intersect. The selected `output_policy`, `output_schedule` and `early_outputs`
appear in binding reports. The scheduler computes a shared producer once, just
before the first output requiring it, and preserves output order. Zero uses the
existing schedule with shared definitions before outputs. Mutable region
destinations can alias their inputs and retain their separate transfer plan.
The game measurement adapter's optional `--schedules` includes these choices
alongside scratch reuse in its bounded, validated candidate neighborhood.

Ordinary public and private function results can share their pure producers
across an ordered result group. `calculation_result_sharing` (0/1, default 1)
and scopes such as `calculation_result_sharing.vx_entry` control this physical
alternative. All roots must belong to total calculation graphs over stable
snapshots; otherwise their independent evaluation sites remain. Output order
and complete-consumer lifetimes are preserved, with no effect moved between
results. Groups are bounded at 64 roots and must retain shared work to be
selected. Function reports expose `result_policy`, `result_sharing` and
`result_groups`. The calculation tests compare shared/independent, automatic/
fixed, banked and private result paths, including distinct effect snapshots.

Ordinary public results default to a reusable serialization snapshot matching their
solved payload width. Each nonconstant leaf is evaluated once and completely
serialized before the snapshot is reused for the next leaf. This requires two
bytes for a 16-bit payload or four for a 32-bit payload, regardless of the number
of leaves; byte payloads and embedded constants need no serialization snapshot.
The original expression's arithmetic, checks and effects remain intact, and
signed interpretation still belongs to the reader. The same byte count enters
`projected_local_bytes` before the existing activation and RAM-budget rules
select automatic or exclusive static storage. These are projected local bytes,
not a measurement or proof of SDCC's stack use.

`calculation_publication_reuse` (0/1, default 1) and physical-function scopes
such as `calculation_publication_reuse.vx_entry` select that shared slot. Global
and scoped allowances intersect. Zero keeps separate snapshots, each still
using the solved payload width. Reports publish `publication_policy`,
`publication_reuse`, `publication_values` and `publication_snapshot_bytes`.
Here `publication_values` counts leaves needing a multi-byte snapshot; byte
payloads and embedded constants do not contribute.
The alternatives have identical evaluation order; separate fixed slots can
occasionally improve target instruction selection at the cost of more RAM.
`calculation_publication_narrow` (0/1, default 1) has the same scoped allowance
rules. Zero keeps a 32-bit carrier even when the payload is 16 bits. The selected
`publication_carrier_bits`, `publication_narrow` and `publication_width_policy`
are reported separately from the payload format. Wide carriers can reduce
target register spills despite retaining more data. Width and slot reuse form
four bounded physical alternatives; byte payloads still need no snapshot.
`make publication-msx` checks that matrix on the host and Z80 with both automatic
and fixed storage. The ordinary source interface keeps the same logical values
and payload format in every realization.
Each invocation retains its four editable projects and reports in a new
`build/publication-msx/run-*` directory, under `{wide,narrow}-{separate,shared}`.
This preserves earlier experiments and hand edits. Each choice directory contains
the source, independent oracle harness, frame reports and `results.json`; its
two `project-word-*` directories retain the generated SDCC project, assembly,
linked image and emulator observation for the two storage policies.

Quotient/remainder sharing is a separate physical choice:
`calculation_division_sharing` and `value_division_sharing` are 0/1 preferences
with default 0. Their physical-function and binding scopes intersect the
global allowance, for example `calculation_division_sharing.vx_entry` and
`value_division_sharing.qr`. Enable the global allowance before enabling an
individual scope. Equal stable operands at the same closed arithmetic width
and signedness can share `q=x/d` and realize the remainder as `x-q*d`.
Both original operations must already be total. Power-of-two divisors keep
their direct realization, and mutable regions do not participate. The graph
budget bounds the rewrite and exhaustion retains the original plan. Reports
publish `division_policy`, `division_sharing`, and `shared_divisions`; these
are candidate descriptions rather than a speed or stack guarantee. Later
finite realization may remove a shared division entirely. The division oracle
exercises both choices against independent signed/modular arithmetic results.

`calculation_division_unroll` and `value_division_unroll` are separate 0/1
allowances with default 1. Their function/binding scopes intersect the global
choice. Zero replaces non-power-of-two constant-specific helpers with the
compact width-specific division loop, preserving the same completion proof.
Ordinary function reports include `division_unroll_policy`, `division_unroll`,
and `unrolled_division_helpers`; binding reports include the policy and
effective choice. The MSX realization adapter can use a measured bank-size or
compiler-resource rejection to disable unrolling only for the affected owner,
then retry the complete source. It retains unrelated choices and records the
change in `realization_preferences`. Further failures can still reduce that
owner's continuation size. The target test resolves a 32-divisor expression
that exceeds one bank with straight-line helpers, then executes its compact
realization against independently computed results.

`value_linear_terms` and `calculation_linear_terms` permit bounded collection of
repeated additive terms, for example `x+x+y` into `2*x+y`. Their default is 16;
zero disables collection and the maximum is 64. A binding scope or physical
function scope restricts the corresponding global allowance. Reports publish
the exact scoped `linear_policy`, effective `linear_terms`, and
`linear_combinations` count. Equal-width additions, subtractions, constant
coefficients and bit-preserving signedness changes participate. A later sum
can collect an earlier `2*x` with another `x` into `3*x`. Widening/narrowing boundaries
and nonconstant products remain opaque. Collection preserves first occurrence
order, stays inside the graph budget and never reevaluates an effectful value.
A rewrite can require no scratch at all; support-header demand therefore
belongs to the selected expression, independently of its frame size.
Sparse constant products have bounded unsigned shift/add realizations at
8, 16 and 32 bits. Signed products use the same modular bit patterns; conversion
back to the logical result type retains its signed interpretation.

The SDCC realization retains publication provenance for call results and loop
snapshots supplied to an ordinary calculation. A target operand barrier may be
needed even when a graph sees such a value as an input, and even when a further
shift or cast separates a stored calculation from its consumer. Physical
storage changes must preserve that information along with type and value facts.

`backends/msx-neo8/tools/optimize.py` provides bounded preference neighborhoods,
validated artifact selection and an input-keyed measurement cache. Singles are
tried before joint alternatives, up to the candidate/time budgets. Artifact
hashes are checked on cache reads and before returning a selected project.
An editable generated directory can be recorded as a tree artifact: changing,
adding or removing a file invalidates it, and symbolic links are rejected.
The embedding supplies the complete input identity, workload and cost policy;
this helper does not treat estimates or unvalidated outputs as measurements.


`calculation_rotation_helpers` and `value_rotation_helpers` are opt-in 0/1
allowances (default 0), with physical-function and pure value/case binding
scopes. Region bindings retain their established expression realization. The
global allowance must be enabled before a scope can use it. Reports publish
`rotation_policy` and `rotation_helpers`. The emitter recognizes an unsigned
byte OR of complementary shifts of the same stable producer, only while both
shifts remain in the expression. Its private inline C helper composes one-bit rotations, which SDCC lowers to a
short `rrca`/`rlca` sequence at the call site. This avoids the additional call
observed in an earlier candidate, but enclosing register allocation still needs
measurement; the choice does not assert improved stack use. The rotation fixture checks every byte
value and all seven counts, alongside a signed non-rotation control.
