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

Add `--emit-c NEW_DIRECTORY` to a resolved request with `--source` options to
inspect `vexel.h`, `vx_bridges.c` and `source-plan.json`. Unfinished resolution
cannot write a projection. Existing nonempty directories are refused.

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
No fallback to the established Vexel solver is linked into this executable.
