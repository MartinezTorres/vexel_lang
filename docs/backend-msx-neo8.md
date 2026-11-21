# MSX NEO-8 realization

The supported path is the relational C++ solver, an editable C/assembly project,
SDCC Z80 ABI 0, and the bundled megalinker. The earlier frontend's `--emit-msx`,
`--msx-entry` and `__target_*` interface is historical, not this executable's API.

## Generate and inspect

```sh
make -j2
make msx-project SOURCE=examples/closure.vx OUTPUT=build/my-project
make -C build/my-project
make closure-msx
make bank-pressure-msx
```

`OUTPUT` must be new or empty. `VEXEL_OPTIONS='--source entry --budget 64'`
selects unannotated roots. `VEXEL_OPTIONS='--program --mode serial'` instead
resolves all exported entries and build values in the root/import closure as
one program. The serial contract must match the embedding's result lifetimes.
To export without invoking SDCC, use:

```sh
python3 experimental/relational/realize_msx.py examples/closure.vx build/editable
```

The project contains `generated/*.c`, the source/target plans, resident runtime,
`user/main.c`, `user/stack-contracts.json`, `local.mk`, a Makefile, and pinned
megalinker/Z80 model sources. `user/vxp_NAME.c` or `.s` replaces that generated
module; assembly takes precedence. Initial export can retry measured oversized
bank modules with smaller continuation groups. It records every attempt in
`realization.json`. Rebuilding an exported project never replaces user edits.

Existing C/assembly bindings, headers and stack contracts can be supplied before
the first measured build with `--user-directory path/to/embedding`. Its files
are copied into `user/` for every candidate, including retries; a supplied
`main.c` replaces the empty adapter. The copied files are fingerprinted in each
attempt's `user_inputs_sha256`. The output project must be outside this input
directory, so temporary realization files cannot become embedding inputs.
Editing, adding or removing one between attempts
invalidates the previous measurements. This option also works through Make's
`VEXEL_OPTIONS`. External implementations retain their ABI and behavior contracts.

New projections include `program-image.json` and its structured code fragments.
Packaging checks every inventoried artifact against that closed image before
copying it into the project. The target plan carries the same image so ordinary,
typed binding and banked products can be traced to one resolution. Hand assembly
or C changes belong in the exported project's override files; editing a closed
projection before packaging invalidates its integrity check. Historical saved
plans without an image retain the earlier packaging path.

SDCC builds pass through `tools/sdcc_guard.py`. Exported projects share a
per-user compilation lock, so parallel Make jobs cannot multiply SDCC's memory
use. Defaults are a 2 GiB address-space limit and 180 seconds per invocation;
`build/sdcc-resources.jsonl` records peak RSS, duration, exit status and limits.
These are host build limits, separate from the cartridge's RAM budget.
`SDCC_MEMORY_MB` and `SDCC_TIMEOUT` can be set explicitly in `local.mk`. Initial
realization treats a compiler memory/timeout rejection as evidence to retry
compact arithmetic or smaller chunks, as it does for an oversized linked bank.
It never automatically raises the host limits.
The exported Makefile also defaults to `--max-allocs-per-node 2000`, matching
the game's bounded register-allocation search. A different `SDCCFLAGS` setting
requires its own correctness, code-size and timing checks.

Feedback is local to the emitter's `partition_owner`. A rejected module with
unrolled division helpers first retries that function with
`calculation_division_unroll.SYMBOL=0`; its proved arithmetic then uses compact
loops. Other rejected modules tighten their function's continuation limit.
Other functions keep their existing policy.
`realization.json` records effective limits, measured failures and input identity
for every attempt. Changes to source/resource or supplied user inputs between attempts invalidate
the evidence. Failures in resident runtime code, or a dispatcher/indivisible
continuation still oversized after applicable arithmetic choices, stop with evidence instead of repeatedly repartitioning
unrelated code. Successful linking and the final placement checks still decide
whether the selected candidate fits.

## Pure value interface

Generated value and region headers include only the arithmetic support functions
their selected C realization uses. `vx_support_value_base.h` contains the
common macro definitions; independently guarded `vx_support_value_*.h` components
provide the required operand-publication functions. This matters on SDCC, which
otherwise emits unused static functions in every including module. Components
compose in either include order and retain real function boundaries wherever
the operand safeguards require them. Handwritten C can still include
`vx_value_runtime.h` to obtain the complete runtime. The component namespace is
separate from user source/value headers.

Nonzero constant division and remainder can use specialized byte, word or
dword arithmetic, including signed divisors. Powers of two use shifts and
biases; other constants use bounded binary subtraction helpers with private
register arguments. The divisor's conversion is validated first. Finite byte
rules may instead become ROM tables under the same table budget. Ordinary C
and pure bindings share these implementations. A variable divisor whose range
proves it nonzero can use a byte, word or dword division leaf with unsigned
magnitudes and exactly that many iterations. A possibly zero divisor retains
its checked runtime path. Variable shifts can also use native operations when
their complete count range fits the original width. Low-bit realization emits
zero when a valid original left shift moves beyond all retained bits.

All C projections now consume the same proved integer range facts. For example,
if every caller of a materialized byte function supplies `n & 7`, its predicate
`n < 8` can become constant without changing its declared byte type. Result
transport and dependent branches can disappear across forwarding calls.
Full C bindings retain their declared client interface while simplifying their
implementation. The `value_ranges` report records the inferred domains and
whether refinement reached a fixed point within its bounded analysis budget.
Entry checks, foreign reply validation, possible traps and recursive completion
remain separate obligations; a known result does not permit dropping its call.

For `examples/range_transport.vx`, the byte argument is masked before a
materialized call and forwarded through another function. With the same
SDCC/megalinker harness checking all 256 inputs, the range analysis measured:

| Quantity | Before | With shared ranges |
|---|---:|---:|
| Generated bank-module code | 171 bytes | 122 bytes |
| Sum of native result-struct sizes | 4 bytes | 2 bytes |
| Observed stack peak | 58 bytes | 52 bytes |
| Simulated Z80 ticks | 1,014,015 | 919,807 |

Both runs returned the expected values without faults. The result-struct sum
is a transport metric, not a stack bound. Timing includes the common harness
and its result checks; the roughly 9.3% reduction is this workload's observation,
not a general game speed claim. Evidence and editable projects are retained in
`build/range-facts-measure/comparison.json` and its sibling directories.

Proved ranges also select smaller private arguments, captures, result fields
and loop cells. Their source types still determine arithmetic and conversions;
the generated header describes the selected private C interface. Public entries
and foreign interfaces retain their declared layouts. Region bindings consume
the same private loop layout, and static-frame budgeting uses actual selected
sizes. The `transport_types` records make the logical/storage distinction
inspectable before linking or editing an implementation module.

For `examples/transport_widths.vx`, two materialized functions declare `#u32`
arguments/results but can transport their values in bytes. The comparison of
`n+1` with 255 still uses 32-bit semantics. With an identical SDCC/megalinker
harness checking all 256 byte inputs:

| Quantity | Before | With private width selection |
|---|---:|---:|
| Generated bank-module code | 271 bytes | 189 bytes |
| Sum of native result-struct sizes | 8 bytes | 2 bytes |
| Observed stack peak | 82 bytes | 56 bytes |
| Simulated Z80 ticks | 1,363,556 | 1,132,090 |

Both runs passed without faults. The roughly 17% time reduction includes the
common harness and is specific to this example; the observed stack peak is
not a proved bound. Evidence and editable projects are retained under
`build/transport-widths-measure`, including `comparison.json`.

Call contexts now query the same function value graph with their own argument
and capture bounds. Global caller unions still determine the shared native
interface. In `examples/call_contexts.vx`, two invocations of one captured-state
update establish 3..10 and 6..13 for their distinct results. That information
also closes the shared capture/result ranges tightly enough to use byte
transport. The two calls and their snapshots remain in the generated program.

The same SDCC/megalinker harness checked all 256 inputs before and after:

| Quantity | Before | With contextual queries |
|---|---:|---:|
| Generated bank-module code | 263 bytes | 186 bytes |
| Native result-struct size | 4 bytes | 1 byte |
| Observed stack peak | 58 bytes | 44 bytes |
| Simulated Z80 ticks | 1,839,315 | 1,601,071 |

Both runs passed without faults. The roughly 13% time reduction includes the
common harness and is specific to this workload. The stack observation is not
a bound. Evidence and editable projects are under `build/call-contexts-measure`.
Packaged plans preserve the per-call bounds and context-budget report in
`value_ranges`; reaching a budget retains a general implementation.

Branch queries extend that mechanism to conditional results and loop transfers.
In `examples/conditional_facts.vx`, a helper returns `n+1` when `n<128` and zero
otherwise. Even an unrestricted `#u32` caller therefore receives a result in
0..128. Both native functions retain their full-width argument interface while
their shared private return structs shrink to bytes. The source arithmetic and
public C interfaces remain unchanged.

The same SDCC/megalinker harness checked all 256 byte-entry inputs before and
after this change; host tests also check the unrestricted entry at 32-bit
boundary values:

| Quantity | Before | With branch queries |
|---|---:|---:|
| Generated bank-module code | 362 bytes | 343 bytes |
| Sum of native result-struct sizes | 8 bytes | 2 bytes |
| Observed stack peak | 82 bytes | 70 bytes |
| Simulated Z80 ticks | 1,338,033 | 1,244,721 |

Both runs passed without faults. The roughly 7% reduction in ticks includes the
common harness and applies to this example. The stack observation is not a
proved bound. Evidence and editable projects are under
`build/conditional-facts-measure`. Separate host and Z80 cases exercise signed
clamps, wrapping conversions, short-circuit guards, captured snapshots, foreign
validation and guarded loop transfers, including the region binding projection.

The input union now includes each invocation's execution predicates as well.
In `examples/guarded_inputs.vx`, both public entries call a shared forwarding
helper only when the input is below 128. That condition establishes byte-sized
arguments through both native helpers. Their 32-bit arithmetic still produces
the correct value 128 at the upper included input, and public arguments retain
their original types.

The same SDCC/megalinker harness checked all 256 byte-entry inputs before and
after. Host checks also exercised the unrestricted entry at 32-bit boundaries:

| Quantity | Before | With guarded input domains |
|---|---:|---:|
| Sum of native argument sizes | 8 bytes | 2 bytes |
| Sum of native result-struct sizes | 8 bytes | 2 bytes |
| Generated bank-module code | 448 bytes | 356 bytes |
| Observed stack peak | 80 bytes | 52 bytes |
| Simulated Z80 ticks | 1,054,513 | 941,161 |

Both runs passed without faults. The roughly 10.75% tick reduction includes the
common harness and applies to this example; the stack observation is not a
proved bound. Evidence and editable projects are in `build/guarded-inputs-measure`.
Regression cases retain full inputs for an unguarded caller, distinguish loop
condition calls from body calls, exercise condition-side updates and nested
loops, and preserve capture conversions, foreign validation and traps.

Shared proof queries reduce repeated analysis of equivalent result fields and
branch expressions. `value_ranges.contexts` now reports cache hits, evaluated
queries, peak contextual work and distinct abstract environments. The existing
node, call-context and depth limits remain unchanged. This is solver work
sharing: it does not cache runtime calls, merge capture snapshots, or require
larger SDCC modules. `examples/shared_proofs.vx` and its host/Z80 regression check
exercise a four-field guarded result with full-width public inputs. These
analysis counters alone do not imply a game speed, stack or code-size gain.

`--emit-c` also offers `vx_value_NAME.h` for eligible source observations.
The `value_projection` entry in each source report records availability and
the reason, header name, entry name, shared values and declared temporary
bytes. The optional header exposes:

```c
vx_NAME_result result;
vx_NAME_value(&result, /* flattened input values */);
/* result.v0, result.v1, ... follow the reported output leaf order. */
```

This `static inline` C interface uses caller-owned typed results instead of
the shared byte arena and indexed readers. It needs no heap or persistent
state. The header can be embedded in an existing C bank; the C compiler decides
whether to inline it. Source input-domain checks remain in the full interface.
The original arena/lease interfaces remain available with their existing ABI.

The current policy recognizes 8/16/32-bit integer and Boolean value graphs:
wrapping arithmetic, casts, comparisons and conditionals, proved valid shifts,
and division/remainder by proved nonzero divisors. It interns repeated expressions,
shares their computed values, and factors common result guards. Constant signed
division preserves truncation towards zero. A trace containing a dynamic
shift/divisor without a domain proof, schema check, load/store, call or loop is conservatively excluded;
this is not an assertion that such a trace cannot be optimized. Both branches
must be certified. Inline policy budgets are 512 graph nodes and 64 result
blocks; exceeding them leaves the ordinary projection available.

Within a factored guard, result leaves independent of that guard are written
once before the split. Nested splits retain the original result field indices.
Shared calculations still use stable temporaries; parameters already have
their SDCC volatile storage and need no second snapshot. A guard is stored
only when its value has another consumer, and its known Boolean value can be
used inside each arm. These transformations apply only to the certified total,
effect-free graph. They preserve input checks, wrapping and result ownership.

For a result selection and unrestricted native input domains,
`vx_value_NAME_cases.h` additionally provides `vx_NAME_branch(...)` and
`vx_NAME_case0_value(...)` / `vx_NAME_case1_value(...)`. `branch_inputs` and
`case_inputs` list indices into the flattened source input list. Invariant
outputs, independent selections and a returned predicate can accompany it.
The first selected result leaf supplies the partition; existing common-guard
interfaces retain their original predicate. The generated branch inspector is
the authority for choosing a case. Each case
uses its established Boolean facts before collecting these inputs; a case may
therefore need no input fields at all. These facts remain confined to the chosen
case and its unchanged snapshot. Case 0 is
false, case 1 true. The adapter **must select the matching case from the same
input snapshot, with no intervening effects**. These case functions are lowered
implementation fragments, not independently validated source activations.
The full interface is appropriate when the adapter cannot meet that contract.

An exclusive activation proof also permits `vx_fixed_NAME.h`, provided every
input has its full native domain. It exposes an alternative interface:

```c
vx_NAME_fixed_frame.a0 = input0; /* Set every input field. */
vx_NAME_fixed();
/* Consume/copy vx_NAME_fixed_frame.result before reusing this frame. */
```

Inputs, shared temporaries and results have named static RAM addresses. This
removes the C parameter/result-pointer ABI and gives SDCC direct addressing for
those fields. Every temporary assignment still executes on each invocation;
the storage does not introduce persistent language state. SDCC may still use
the stack for return addresses, spills and arithmetic helpers.

`value_projection.fixed_frame` reports availability, the reason, header,
entry, frame symbol and exact Z80 byte count, checked by a target `sizeof`
assertion. Eligibility uses the closed frame graph's activation ownership,
recursion and boundary analysis. Restricted inputs retain the automatic
interface because their validation may call the embedding's trap handler.

The embedding owns the complete interval from its first input write until it
has consumed or copied the result. No callback or concurrent invocation may
reuse that frame during this interval. Values retained across callbacks need
separate storage even when the source graph itself is serial. Each including
translation unit owns a separate frame. Inclusion selects the allocation;
unused emitted headers allocate nothing. The ordinary `frame_ram_bytes` budget
continues to govern ordinary arena frames, so an embedding must separately
budget the fixed interfaces it selects and verify final linked RAM. Edelweiss
does this with a Vexel budget value, a generator check and the linker RAM limit.
These manually selected interfaces do not share allocations. Named bindings
below allow the solver to select and overlay storage.

### Solver-selected C bindings

An embedding can delegate the storage choice to Vexel using module-level
metadata on a source definition:

```vexel
[[prefer(value_frame_bytes, workspace)]] workspace=128;
[[c_binding(step, full, 16, exclusive)]]
&^step(x:#u8){x+1}
```

The arguments are a unique binding identity, entry (`full`, `case0`,
`case1` or `region`), relative call frequency (1..1000000), and lifetime contract
(`exclusive` or `open`). Frequency is an ordinary resolved Vexel expression
and is a cost hint; it cannot select a semantic source answer. The annotation
requests its source projection, like `[[source]]`. Case entries require the
existing certified dependency split. Unsupported value graphs and invalid
case requests fail explicitly.

`exclusive` is a contract supplied by the external C engine: no invocation of
the same binding may overlap from the first input write until result
consumption. This includes callbacks, concurrent sources and any retained
result. Copy values needed beyond that interval. The solver additionally
requires its own activation exclusion and, for value entries, total-graph proof; the annotation
cannot override them. `open` retains automatic storage, including for a pure
source. Vexel does not infer these external lifetime facts by analyzing C.

`[[c_disjoint(step,other_step,third_step)]]` on a module declaration supplies
an additional embedding contract: every pair listed has disjoint complete
input-preparation through result-consumption intervals. Declarations may
precede the named bindings and span imports. Unknown identities and repeated
identities within one declaration are errors. Exclusion is not transitive:
excluding A/B and B/C does not exclude A/C. This contract does not establish
self exclusion, close a source graph, or promote an `open` binding.

The deterministic policy orders eligible bindings by decreasing frequency,
increasing frame size, then identity. For each candidate it considers a new
slot and existing slots whose every member explicitly excludes the candidate.
It chooses the least incremental RAM cost, breaking ties by slot order. A
shared slot costs the largest member's Z80 frame size. Candidates exceeding
the remaining `value_frame_bytes` allowance remain automatic. Without pairwise
facts the policy retains distinct frames. This is a feasible heuristic, not
a measured cycle-cost model or optimal packing. The report's `value_bindings`
records physical `static_bytes`, logical `unshared_static_bytes`, slots and
their members, exclusion origins, and each binding's decision, input
dependencies and slot (`null` for automatic storage).

Both choices expose the same generated `vx_bind_step.h` interface:

```c
VX_BIND_step_LOCAL;
vx_bind_step_frame.a0 = input;
vx_bind_step_run();
output = vx_bind_step_frame.result.v0;
```

The declaration macro creates local storage for the automatic choice and is
empty for the fixed choice. The run entry is a parameterless function for
fixed storage, or a wrapper passing the automatic frame's address. It preserves
volatile operand safeguards. A real function boundary separates input publication
from computation: expanding the calculation into the caller exposed incorrect
SDCC byte-operand forwarding in the game. Set all reported inputs before each run.
For a case, first evaluate the matching generated predicate and retain one
unchanged input snapshot. No effects may intervene between guard and case.

Compile the emitted `vx_binding_frames.c` exactly once, in its own translation
unit. `vx_binding_types.h` defines typed frames; `vx_binding_storage.h` declares
one typed C union per selected slot. Binding headers name the corresponding
union member, so multiple callers share the budgeted allocation without byte
arena casts. A result must be consumed or copied before any other member
reuses the slot. Automatic frames belong to their lexical caller scope.
The fixed total counts inputs, temporaries and results, with Z80 `sizeof`
assertions on frames and unions and final linker RAM checks. Host unions may
contain additional ABI alignment padding. Lowering the
budget with `--prefer value_frame_bytes=0` changes storage without editing the
client. `make value-msx` executes that same client under automatic, distinct
fixed and overlaid plans. It also checks partial budgets, mixed-width frames,
nontransitive exclusions and open/nested lifetimes on the host.

### Execution-region bindings

`[[c_binding(move,region,60,exclusive)]]` requests an entire ordered execution
with a typed frame. The region can contain native integer calculations,
conditionals, nested loops, break/continue guards, and synchronous external
calls. Boundary arguments and replies retain their native widths. Effects
execute in trace order; expression sharing never crosses a boundary call.
The ordinary projection remains available alongside this realization.
An explicit region request fails for unsupported operations, including native
function invocations, persistent loads/stores, loop return payloads, non-native
integer interfaces, and arithmetic needing runtime checks. It never silently
drops an operation to obtain a cheaper candidate.

The generated header exposes a boundary definition and data macro:

```c
VX_BIND_move_BOUNDARY_observe {
    VX_BIND_move_DATA_observe.r0 = engine_query(
        VX_BIND_move_DATA_observe.a0);
}
```

Arguments are `a0...`, replies `r0...`, in flattened declaration order.
Write every reply before returning; do not retain the buffer. The adapter must
implement the declared external operation, including its state observations.
Fixed regions use a parameterless boundary and their owned frame; automatic
regions pass a caller-owned typed buffer. Both use the same adapter macros.
Boolean replies and explicit schema/index checks remain checked. Native C
reply types already constrain their representable integer widths.

The `exclusive` interval includes all boundary calls. An engine callback may
invoke another binding whose workspace is separate, but must not reenter this
binding or overwrite any overlapping workspace. This is a scoped embedding
contract, not a `no_feedback` promise. The projection admits no internal native
calls, and known source overlap still prevents fixed allocation. No exclusion
between a region and a callback binding is inferred from serial execution.

The region planner proves readiness invariants using both entry edges and
backedges, then builds a control-flow graph of parallel transfers, decisions,
checks and synchronous calls. It canonicalizes predicates and propagates known
branch outcomes until a write invalidates them. Edges bypass repeated decisions
whose outcomes they already know. Facts and dead transfers are recomputed after
edges disappear. Common result selections are
split before any destination is written (at most eight splits per transfer).
This preserves the old state needed to decide which new state to accept.

Backward liveness removes dead transfers and identifies reads that still need
entry initialization. An interference graph permits a loop field to reuse a
same-typed cell only when their lifetimes do not overlap. Published input,
result and boundary names retain their logical ABI; scalar coalescing does not
merge distinct ABI fields.
Input publication lends those cells to the occurrence: after their last use,
the implementation may overwrite them with loop state. Read results after the
run and republish inputs before another run; do not expect old input fields to
remain unchanged. Result cells may likewise hold intermediate state until the
run returns.
Callback results are definitions, so reuse cannot hide a value still needed
across a callback. The report includes coalesced bytes and remaining entry-zero
bytes alongside the final frame size. These are physical storage decisions;
they do not weaken Vexel's source initialization rules.

A subsequent allocation may place complete boundary objects in a typed union
when their final lifetimes do not overlap. It includes argument cells reused
for loop state, retained replies, empty calls, and conflicts between every
object read/written by the same transfer. `region_boundary_slots` and
`region_boundary_conflicts` expose the allocation; `region_boundary_bytes`
compares with `region_boundary_unshared_bytes`. Adapters borrow the current
object synchronously through `_DATA_SYMBOL`, preserve inputs and initialize
outputs, and retain neither its address nor an assumption about its identity.
`--prefer region_boundary_overlay=0` selects separate objects. The default `1`
keeps directly expanded native objects dedicated to limit target register
pressure; `2` allows all compatible sharing. `region_boundary_dedicated` records
that cost choice separately from lifetime conflicts. Sharing may reduce
fixed RAM or automatic frame size; target measurements still determine stack,
ROM and timing effects.

Repeated pure calculations in a branch predicate also use owned scratch when
sharing is needed; a large volatile expression need not become Z80 stack spills.
Parallel-copy cycles save old values before overwriting them. Expression scratch
is reused between transfers, with separate slots across branches within a
transfer. `--prefer region_copy_schedule=0` retains buffered parallel transfers
as a comparison candidate; the default is `1`. By default, transfers with at
least three stores use small generated helpers sharing the region frame.
`--prefer region_transfer_functions=0` keeps them in the enclosing function.
This limits SDCC allocation-search complexity; its extra calls remain part of
the measured stack and timing cost. Frame sizes include inputs,
outputs, boundary buffers, live state and scratch. They are not stack bounds.

`region_predicate_functions=1` permits bounded byte predicates to borrow the
frame through a generated pure function. A fixed occurrence passes no scalar
arguments; an automatic occurrence passes its frame pointer. The function
boundary isolates previous stores while its read-only body can use direct
byte arithmetic. It returns the predicate's truth value at the original branch
or check. Identical predicates share code, with no result caching across calls.
The report counts definitions and call sites. Preference zero keeps the prior
generic arithmetic helpers. Wider or more complicated expressions retain their
existing safeguards; actual target measurements determine whether the selected
functions improve speed, stack use and code size.

### Bounded measurement search

`tools/optimize.py` in this backend implements a finite compile/validate/measure
selection loop. An embedding supplies candidates, an evaluator, an objective,
and regression/target limits. The baseline must pass first. Failed, timed-out,
unvalidated, over-budget or regressing alternatives cannot replace it. The
report records each decision and hashes the measured artifacts; a winner whose
artifact has subsequently changed is rejected. The report also retains a
`frontier` of accepted alternatives that are not dominated on all objective
metrics; equal costs keep the earlier candidate. Their artifacts are rechecked
before `artifact_validation` becomes `passed`. This exposes measured speed,
stack and ROM tradeoffs to a future policy without changing the current
lexicographic selection. Measurements select physical
realizations, never source values or weaker semantic requirements.

The concrete `ext/edelweiss` driver is `make optimize-player`. It compares the
existing C-controlled binding chain, a complete Vexel movement region in the
player's bank or a separate bank, and buffered region transfers in isolated
editable SDCC projects. Host UBSan traces,
SDL encounters and MSX encounters validate behavior before callback timing and
stack samples are ranked. Stack must not increase; each measured actor group
and the crowd encounter has a 2% timing allowance, and linked globals must end
before the map arena. A RAM choice must not quietly regress another callback.
The search is limited to four candidates and a wall-time budget. Its output is
an evidence report and selected project, not a new semantic proof or a
worst-case execution-time bound. It does not overwrite the working project.
The Makefile passes its current workspace and branch-specialization budgets
into the baseline. Reusing a baseline also requires matching source files,
compiler, selected tool executables and baseline preferences; old reports
without that input identity require a fresh search. The driver checks these
common inputs around each measurement and before publishing the selection.

The ghost embedding in `ext/edelweiss` uses this split to check cycle/slot
before reading the movement inputs. The Vexel program still describes one
complete update, including both axes and frame selection. No game-specific
arithmetic macros or manually materialized velocity helper are needed.
The skeleton embedding uses its generated delay guard after obtaining random
inputs. Its waiting case takes five inputs, omitting anchors, player position,
clock and selection draws. The full source and full value API remain available.

The value headers retain SDCC operand safeguards and explicitly select its
two's-complement casts/arithmetic shifts for Z80. Host formulas avoid signed
overflow and negative C shifts. General multiplication uses the existing
freestanding `vx_mul` on Z80. `make value-msx` checks the value, case and fixed interfaces against
independent integer vectors on the host and the Z80 simulator, including the
shared-byte-operand regression. This is a feasible lowering policy, not a
globally optimal cost search or a certification of every SDCC expression.

The ordinary `frame_planning`/arena report describes the ordinary ABI;
`declared_local_bytes` is not a stack bound or `sizeof(result)`. Alternative
inline code and result storage belong to their C caller. Linked section sizes,
assembly stack analysis and workload measurements must cover that embedding;
automatic bank continuation splitting does not apply to hand-written callers.

## Configuration relationships

Annotations and CLI contributions use the same solver relationships. Their
numeric arguments are ordinary Vexel expressions, including imported values,
record properties, function calls and finite choices. Configuration is not
resolved by an independent evaluator before source constraints run.

```vexel
#Limits(payload:#u16, stack:#u16);
[[require(payload_bytes, limits.payload)]]
[[require(stack_bytes, limits.stack)]]
[[prefer(frame_ram_bytes, 128)]]
limits=#Limits(64,1024);
[[source]] &^frame(n:#u8){n+1}
```

| Relationship | Role | Default / supported range |
|---|---|---|
| `ram_bytes` | fact | 16384 in this resident profile |
| `rom_bank_bytes` | fact | 8192 |
| `payload_bytes` | requirement | 64 / 0..65535 |
| `stack_bytes` | requirement | 1024 / 32..12288 |
| `mapper_segments` | requirement | 0 / 0..255 owned extra segments |
| `call_depth` | requirement | 0 (unbounded) / 1..65535 guarded bank entries |
| `stack_proof` | requirement | 0 / 1 requires a closed assembly bound |
| `frame_ram_bytes` | preference | 0 / 0..12288 local-buffer allowance |
| `value_frame_bytes` | preference | 0 / 0..12288 selected C binding allowance |
| `value_table_bytes` | preference | 512 / 0..2048 immutable lookup bytes per pure realization |
| `value_table_search` | preference | 16 / 0..64 computed-intermediate table candidates |
| `value_table_bytes.BINDING` | preference | optional binding cap, combined with global allowance by minimum |
| `value_table_search.BINDING` | preference | optional binding cap, combined with global allowance by minimum |
| `region_copy_schedule` | preference | 1 / 0..1 schedule parallel loop transfers, or retain buffered transfers |
| `region_transfer_functions` | preference | 1 / 0..1 bound SDCC allocation regions with shared-frame transfer helpers |
| `bank_chunk_operations` | preference | 128 / 1..4096 continuation operations |
| `bank_chunk_operations.SYMBOL` | preference | optional / 1..4096, tightens one emitted function's limit |

CLI forms are `--fact KEY=N`, `--require KEY=N`, `--prefer KEY=N`.
`--budget N` and `--frame-ram-bytes N` contribute the corresponding relationships.
Repeated hard requirements conjoin: different explicit values conflict, including
two values from the CLI. Defaults apply only when nobody supplied that key.
Multiple allowances combine by their minimum, independent of origin/order.
Preferences choose a physical policy; they do not choose a source answer.
Unknown relationship keys are errors. Unknown ordinary metadata remains inert.
Reports retain contribution origins, including on contradictory requests.
For example, `--prefer bank_chunk_operations.vx_entry=16` or
`[[prefer("bank_chunk_operations.vx_entry",16)]] policy=0;` limits the emitted
`vx_entry` function. Its effective limit is the minimum of global and scoped
contributions. Symbols come from `bank_modules[].partition_owner`; a scope
without an emitted function is an error when projecting a runtime program.
These identities belong to a particular closed projection, especially for
specialized helpers; measurements must not be reused across input revisions.
Operational settings such as `--max-work`, output paths and scheduling stay
outside this vocabulary. These hardware facts describe this backend, not every
possible Vexel target.

## Memory and ownership

The BIOS-compatible profile retains page 0 and uses resident RAM starting at
`0xC000`, with stack top `0xF380`. Linked data, resident code, stream bookkeeping
and selected static locals must fit below `stack_top - stack_bytes`. There is
no assumed RAM below `0xC000`. ROM windows are 8 KiB; code wrappers preserve
window B across nested calls. The current export allows 256 ROM segments.
These choices follow the [NEO mapper specification](https://aoineko.org/msxgl/index.php?title=NEO_mapper).

`[[stream(capacity)]] &^frame(...)` creates a bounded pool for repeated events.
Capacity is a solver expression in 1..255. Generated APIs are:

- `vx_frame_submit(...)`: returns a nonzero lease, or zero for full-pool,
  execution-gate or token-space backpressure.
- `vx_frame_read_lease(lease,index)`: reads an owned scalar result leaf.
- `vx_frame_release(lease)`: relinquishes ownership, returning whether it did so.

Submission serializes execution across the generated pools. Results can remain
live together and can be released in any order. Nested submission from a sink
returns zero. The token space is shared by the projection and never wraps;
a stale token cannot silently become another result. Lease reads after release
trap with reason 21. The raw `vx_frame`/`vx_frame_read` functions are internal
scratch operations in this mode: adapters use the lease API. The pool is not
an atomic multi-thread API; an adapter for concurrent boundary events must
serialize access to it. Finite named occurrences and explicit overlap graphs
remain available for other activation contracts.

`[[mapper]]` attaches a placement requirement to an owned persistent global.
Constants can still be embedded without storage. Live mapped globals consume
native scalar bytes in a separate logical address space; their total must fit
`mapper_segments * 16384` in the source solve. Uninitialized and lazy-initialized
locations retain the ordinary readiness checks. No mapped address escapes into
Vexel, and mapper storage does not enlarge the resident C stack.

The embedding adapter must first call `vx_mapper_init(slot, first_segment,
count)` to grant exclusive ownership of a consecutive set of physical RAM
segments. This is an explicit grant, not an installed-RAM probe or DOS allocator.
A DOS adapter can reserve segments before making the grant. Accessors borrow
page 2, use port `0xFE`, and restore the cartridge slot and interrupt state.
The profile owns the RAM mapper's page-2 selection register; other adapters must
honor that ownership. See the mapper manufacturer's
[register specification](https://map.grauw.nl/resources/sound/musical-memory-mapper.pdf).
The baseline has no such grant. An uninitialized, repeated, or out-of-range grant
traps with reason 22. The integration example tests both the resident baseline
and extra mapper storage, including an access crossing a segment boundary.

`[[msx_address(expression)]]` and `[[msx_port(expression)]]` bindings also demand
ordinary source expressions. Absolute resident bindings are checked against
linked ownership and the stack reservation.

## Bank cuts and stack contracts

Small functions remain editable whole-function modules. Larger generated
functions become continuation groups with an explicit target frame and program
counter. Branches, loop back edges, break/continue and results cross those cuts;
source calls and recursive frames retain their ownership. Generated frame
headers make the carried state inspectable. Each group is a window-B module.
Their program counter and carried parameters add target bookkeeping beyond the
source local-buffer allowance; the final linked-RAM check includes these bytes.
The measured object sizes can cause initial realization to retry smaller groups.
An indivisible operation or a resident arena that cannot fit is a target failure,
not a silently truncated program. Groups are bounded by a 16-bit control-state
index. An operation count is a partition policy, not a claim about Z80 byte size.

The 8192-byte constraint applies to each indivisible bank allocation's machine
code and constants. C source bytes and textual `.rel` file sizes are not ROM
sizes. A whole-program solve may produce many independently placed modules.
Each `bank_modules` record includes `partition_owner`, `partition_role`
(`whole`, `dispatcher`, `continuation`) and the effective `chunk_operations`.
The initial realizer measures `_CODE` plus `_CONST` in each generated object
and first disables any demanded constant-division unrolling for a rejected
owner, then halves that owner's group limit down to one operation. The
`realization_preferences` field records the arithmetic choice separately from
`partition_limits`. A 32-divisor expression measured 29,520 bytes with its
straight-line helpers and 2,311 bytes after this compact retry, without changing
its continuation policy; its independent Z80 oracle passed. Smaller groups
can increase dispatch overhead or total ROM size; the search seeks a valid
bounded realization and does not claim a globally optimal partition.

`call_depth` adds a shared counter to generated bank wrappers. Exceeding the
explicit depth bound traps with reason 23. Zero retains unbounded source
recursion and cannot supply a finite recursive stack proof.

After SDCC, `tools/stack_usage.py` traverses assembly control flow, SP adjustments,
checked switch tables and the actual call graph. It handles balanced runtime
loops and depth-bounded generated recursion. Unknown SP changes, indirect calls,
unguarded recursion and missing external contracts leave the proof open. It
never substitutes observed stack use for a bound. Changing guard/trap code
invalidates the inherited recursion bound.

Different temporary stack depths may meet at an unconditional restoration from
a proved common frame address. The checker retains each incoming peak before
merging the restored state. Checked SDCC switch tables also remain recognizable
across register pops that preserve HL; changing the jump address or losing the
saved frame address leaves the proof open. The bank-pressure example requires a
closed stack proof alongside measured bank partitioning and external responses.

`user/stack-contracts.json` supplies `environment_bytes` for embedding/concurrent
boundary frames and a `functions` map of assembly symbols to maximum additional
stack bytes (excluding the return address). BIOS routines and handwritten
adapters need real contracts. These are explicit assumptions, recorded in the
report. The bound covers each named source entry; it does not prove arbitrary
user code's stack use. A bound exceeding the reserve rejects the realization.
`[[require(stack_proof,1)]]` also rejects an open proof.

The generated `resolution.json` binds placement, stack results, assumptions,
input hashes and object hashes to the actual ROM. `simulate` checks that ROM
and records conditional workload evidence. Its Z80 core runs real linked code;
BIOS calls are stubs. Optional `SIMFLAGS='--ram-segments 3'` supplies a model
mapper in slot 3. There is no VDP, audio or timing-accurate MSX model.

`stack_analysis.value_bindings` separately reports the SDCC computation helpers
for named C bindings. Each emitted translation-unit copy has its own bound or
an explicit unknown reason; an unused helper is marked `not-emitted`. The bound
includes its return address, spills and callees. It excludes the caller's input
publication, automatic frame and surrounding engine frames, and does not extend
the source-entry `stack_proof` contract. A fixed binding's `frame_bytes` is RAM
storage, not a hardware-stack bound. Unknown helpers never receive a zero bound.
Reachable raw assembly bytes or unrecognized emitting directives also leave the
bound open; literal pools that control flow skips do not affect it.

The pure C generator favors bounded branch expansion for wide selected stores,
direct predicates and byte transfers/comparisons of stable wide fields on Z80.
These choices preserve the existing SDCC operand safeguards and require no new
source annotations or RAM. See [the operand notes](sdcc-operands.md) for the
limits of these heuristics. Backend flags remain editable in `local.mk`; stack
savings alone do not justify an option that makes frequently used code slower.

Static local selection still depends on the proven source call/activation graph.
Before that selection, a shared dependency closure removes unused native call
arguments, incoming/returned capture leaves and loop-carried state. Retained
results use native-width struct members; a byte result needs one byte. Native
calls without results have no output pointer. Generated bank continuations carry
the typed interfaces intact. The `runtime_functions` report maps logical leaves
to transport and records target byte widths; `interface_demand` records the
closure and retained loop state. Before demand closure, immutable value facts
can replace native results with constants or unchanged inputs and remove loop
copies of invariant inputs. Inductive readiness proofs remove initialization
flags only when they hold at entry and after every transfer. Mutable call actuals
retain snapshots; known results do not prove completion. These facts and the
transport mappings appear in the packaged plan. Calls,
potentially trapping evaluations and foreign reply validation remain obligatory.
Use zero `frame_ram_bytes` before editing generated modules or overriding them;
otherwise changes invalidate that proof and are rejected. C/assembly edits are
inspectable escape hatches whose source-semantic correctness requires review.

`examples/interface_demand.vx` demonstrates the interaction: one result of a
two-result function is consumed through a forwarding function. The unused
16-bit history passes through a loop and both calls. Demand closure removes that
history, its calculation, and other unused carried values without inlining the
functions. Compared with the preceding result-only pruning implementation, a
linked SDCC/megalinker run over all 256 byte inputs measured:

| Measurement | Result-only pruning | Joint demand and native results | With immutable value facts |
| --- | ---: | ---: | ---: |
| Observed peak stack, startup to first HALT | 132 bytes | 77 bytes | 70 bytes |
| Model ticks for the full workload | 4,649,849 | 2,013,213 | 1,444,893 |
| Occupied ROM sections | 3,566 bytes | 2,844 bytes | 2,476 bytes |
| Resident RAM | 2,031 bytes | 2,007 bytes | 2,007 bytes |
| Declared local buffers in entry / forward / advance | 8 / 4 / 32 bytes | 5 / 1 / 12 bytes | 5 / 1 / 4 bytes |

The additional facts prove that the loop's tick limit equals its immutable
argument and that all six logical carried roots are always initialized. Demand
retains only the changing position and iteration count, each with its parallel
transfer staging byte. No readiness flags remain. The materialized calls and
loop execution still occur.

The padded ROM remains 128 KiB. These are measurements of this example with the
same workload and toolchain, not Edelweiss performance claims. The separate
assembly analysis reports source-entry bounds of 119, 68 and 57 bytes under its
recorded zero-environment allowance; that scope excludes the surrounding test
entry and differs from observed whole-workload peak stack. Generated projects,
ROM hashes and full measurement records are in `build/interface-demand/` for the
comparison runs, with the latest pair in `facts-before/`, `facts-final/` and
`value-facts-comparison.json`. Host UBSan and Z80 tests also cover sparse interfaces, captures,
recursive forwarding, completion-only recursion, traps, readiness, parallel
loop copies and mixed-width results carried through bank continuations. The
immutable-fact tests also exercise call-time snapshots, aliases through
materialized calls, rejected dependent invariants, nested loops, signed
narrowing, and recursion whose eventual result is known but never reached.

`examples/equality_transport.vx` exercises a different relationship: two changing
byte values start equal and receive the same updates. Both are observed through
two materialized calls. Equality proofs let them share one loop cell (plus its
parallel-transfer staging cell) and one member of each native result buffer.
The same mechanism lets a native return and an updated capture share a snapshot.
Different invocations and partially initialized loop leaves remain separate.

Compared with the immutable-fact implementation above, the same linked
SDCC/megalinker workload over all 256 inputs of this new example measured:

| Measurement | Before equality proofs | With equality proofs |
| --- | ---: | ---: |
| Observed peak stack, startup to first HALT | 74 bytes | 70 bytes |
| Model ticks for the full workload | 3,877,739 | 3,596,011 |
| Occupied ROM sections | 2,667 bytes | 2,564 bytes |
| Resident RAM | 2,018 bytes | 2,018 bytes |
| Declared local buffers in entry / forward / advance | 6 / 2 / 6 bytes | 5 / 1 / 4 bytes |

The padded ROM remains 128 KiB. The separate source-entry assembly bound falls
from 61 to 57 bytes under the recorded zero-environment allowance. These figures
belong to this two-result example and should not be compared directly with the
preceding one-result workload. Generated projects, hashes and measurements are
in `build/equality-facts/`, including `comparison.json`. The packaged plan carries
`result_aliases` and loop `carried_aliases` alongside their physical interfaces.
Host and Z80 checks cover shared returned captures across bank continuations,
sparse consumers, rejected dependent equalities, partial initialization,
distinct calls, differing types, traps, foreign validation and nontermination.


The generic measured-search helper accepts an optional `priority` order for
`preference_variants`. It reorders scopes within each neighborhood distance;
the checked baseline still comes first and every candidate retains the same
validation and cost requirements. Duplicate or unknown identities are rejected.
This gives an embedding a place to use source frequency hints (or a future
external heuristic) without promoting the hint into a semantic fact or a
measurement. A bounded search may still stop before finding a better candidate.
