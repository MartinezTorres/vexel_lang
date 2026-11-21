# Preserving shared operands in the SDCC projection

The relational schema oracle exposed incorrect byte arithmetic in the locally
available SDCC 4.2.0 #13081, 4.5.0 #15242 and 4.6.0 #16555 Z80 compilers. This
minimal C example reproduces the assembly error with `-mz80 --sdcccall 0
--opt-code-size`:

```c
#include <stdint.h>
void save(uint8_t, uint8_t, uint8_t) __sdcccall(0);
void example(uint8_t x) __sdcccall(0) {
    save(x, ((uint32_t)x + 1UL) & 255UL,
            ((uint32_t)x + 2UL) & 255UL);
}
```

The generated code loads `x` into A, adds two in A, and then reuses that modified
register for both the first argument and the addition of one. With zero input,
the arguments become `(2,3,2)` instead of `(0,1,2)`. Disabling peepholes, GCSE,
LOS PRE, or changing frame-pointer and allocator settings did not remove the
observed failure. This is a local reproducer, not a claim that all arithmetic
in these compiler releases is incorrect.

The relational C projection conservatively keeps residual input parameters in
volatile automatic storage under SDCC. Reads of call-result and loop snapshots
also use volatile lvalues of their actual storage type. The host projection
retains ordinary reads. The result buffers' pointer ABI stays unchanged.
These accesses implement stable Vexel values; they do not introduce external
effects, shared scratch storage, or new language semantics.

This costs loads and stack space. A future realization may remove individual
barriers after certifying the selected toolchain and expression family. A
version number alone does not justify removing them. Generated C, assembly,
compiler flags and execution evidence remain inspectable in each project.

`make relational-schema-msx` exercises the original failing aggregate store
over every byte input, including wraparound, and compares host and Z80 results.
It also checks that invalid schema writes and constructors reach trap 19.
See the completion ledger for the latest fingerprinted run.

The optional pure value projection keeps the same volatile parameter safeguard
and uses narrow volatile temporaries for shared computations. Unlike the
ordinary arena interface it need not serialize every leaf through a 32-bit
snapshot. When a common guard is split from its cases, only the predicate's
parameters are acquired before selecting a case. This avoids eager copying of
inactive inputs without removing the operand safeguard. `make value-msx`
checks the new interface, its split cases, byte wraparound and signed arithmetic
on Z80. The full Edelweiss ghost callback is also exercised with the game's
different SDCC flags in openMSX.

Factoring a result guard no longer copies already-volatile input parameters
into a second set of volatile locals. Computed values shared across the guard
retain their stable storage, while a guard used only for branching is tested
directly. Branch-independent output stores are emitted once with their original
field indices. The `partial` value oracle exercises interleaved invariant and
conditional fields, a calculation shared with its guard, nested selection and
byte wrap on both host and Z80. The mixed Edelweiss encounter checks the same
policy in the game's SDCC build. Volatile parameter and arithmetic safeguards
remain in place; this change does not rely on removing those barriers.

The fixed-address value alternative preserves the barriers in named RAM fields
instead of volatile stack parameters and automatic temporaries. Inputs are
written explicitly, the generated no-argument function computes directly into
its frame, and the embedding consumes the result before relinquishing ownership.
Ownership spans input preparation through result consumption. Every temporary
assignment still runs on each activation; C static initialization is not used
for computations. The ordinary caller-owned result interface remains available
for overlapping or independently retained invocations. `make value-msx` executes
both realizations against the same integer vectors and checks the Z80 fixed
frame sizes. Input checks that can trap, effects and unproved activation
exclusion do not acquire fixed frames through this alternative.

Selected C bindings keep a real computation function boundary after input
publication. An initial macro-only candidate let SDCC forward a stored skeleton
slot while clobbering the separately loaded cycle in A. In the game's adapter,
`((cycle+slot)&3)==0` then effectively used the slot twice: cycle 1, slot 4
incorrectly passed the cadence check. Both input fields were volatile, so those
qualifiers alone were insufficient. Fixed bindings now use parameterless
functions; automatic bindings pass their local frame's address. The generic
Z80 binding oracle includes cadence and the full game encounter requires all
crowd decision traces to match. A call/return cost remains, while fixed inputs,
temporaries and results retain direct RAM storage.

The bomb transition exposed another forwarding case with SDCC 4.2.0 and the
game's `--max-allocs-per-node 2000` flags. After storing a selected byte `step`,
the expression `counter + step` loaded counter into A, immediately overwrote A
with step, and added C (which also held step). Counter zero and step one thus
produced two. Both fields were volatile and the enclosing binding already had
a real function boundary. The saved listings are in the game's
`build/projectiles/sdcc-before-add8.asm` and `sdcc-after-add8.asm`.

The pure value emitter now isolates byte addition through a small target-only
function when either dynamic operand is a materialized computation in that
function. Its two volatile byte parameters separate operand publication from
addition. Constants and input-only additions retain their existing emission;
using the helper for every byte addition caused unnecessary overhead in the
game's cadence checks. Host addition remains ordinary unsigned arithmetic.
The helper is pure, uses ordinary call-stack storage, and does not borrow a
binding's shared frame. This does not change source arithmetic or prove a
whole-program stack bound.

The pickup lifetime port exposed the same forwarding failure in an ordered
byte comparison. With SDCC 4.2.0 and `--max-allocs-per-node 2000`, a stored
`room = counter < 240 ? 240-counter : 0` followed by
`step = elapsed < room ? elapsed : room` loaded elapsed into A, overwrote A
with room, and subtracted C, which also contained room. The first update
therefore selected 240 elapsed ticks instead of one. The actual MSX encounter
caught the immediate expiry; the host comparison passed. The game's
`build/passive-drop-debug/before.asm` and `events.txt` preserve that evidence.

The value emitter now also isolates unsigned byte ordering through a target
helper when either dynamic operand is a materialized computation (or inputs
are mutable). `<`, `>`, `<=` and `>=` use one strict comparison, reversing or
negating it as required. Constants and immutable input-only comparisons retain
ordinary emission. The helper evaluates each operand once, preserves the
unsigned comparison width, and uses ordinary call storage rather than borrowing
the binding workspace. Signed and wider comparisons keep their existing
realization. The `clamp` value oracle covers zero room, equality, both orderings
and wrapped output counters in automatic and fixed frames; the game encounter
also exercises the selected binding with the original failing SDCC flags.

The same small regression also exposed a corrupted destination address in the
automatic binding with that allocator cap. While storing a byte result, SDCC
loaded the computed value into the high byte of the destination pointer. The
automatic binding's frame-pointer parameter now uses `VX_STABLE` as well as
its value fields, keeping that pointer in volatile storage on SDCC. This
qualifies the pointer itself; the frame remains owned by its caller. Fixed
bindings retain their direct-address access and host C keeps ordinary pointers.
Both allocation choices execute the ordering oracle on Z80.

The fairy port exposed a related promotion failure: with anchor zero and a
stored height of twelve, `(anchor << 8) + height` became `0x0c0c` instead of
`0x000c`. SDCC reused height for the anchor's high byte. Word addition now uses
an analogous helper when either dynamic operand depends on a stored computation,
including through a cast or negation. Input-only and constant additions keep
their existing emission. The `compose` regression checks positive and negative
displacements, coordinate wrapping and both binding storage choices. The game's
`build/passive-fairy-debug/events.txt` and `build/passive-drop-debug/fairy-before.asm`
record the original failure. These helpers are correctness barriers with a call
cost, not a claimed speed optimization.

The `advance` value oracle covers selected steps, distinct addends and byte
wrapping in both automatic and fixed frames. `make value-msx` checks it on
Z80 along with the existing value and binding cases. The game encounter tests
the larger original failing expression, ordered bomb effects and real map
mutation with the game's SDCC flags.

The value generator also reduces the number of wide values that SDCC must keep
live together. A store involving up to three unmaterialized selections selects
its path first, then computes into the destination. The eliminated selections
retain their original integer conversions; signedness and wrapping do not
change. Larger expressions retain ordinary expression emission to bound code
duplication. Wide control predicates use short-circuit C conditions instead of
intermediate boolean values; numeric result stores retain their numeric values.
Byte-only predicates retain their previous lowering: applying the same rewrite
there increased spills in a measured combat path.

Transfers and equality comparisons between stable, same-type 16/32-bit fields
use volatile byte accesses on Z80. They require neither a full word temporary
nor an additional workspace. These internal macros apply only to owned pure
values, never external I/O, atomic data or effectful lvalues. Host code keeps
ordinary assignments and comparisons. The existing volatile barriers, true
binding call boundary and computed-byte addition safeguard remain in place.
`make value-msx` covers nested word selections, wrapping sums, signed casts,
byte differences throughout wide comparisons and non-boolean selected results.

These are register-pressure heuristics, not promises of zero stack use. SDCC
can still introduce spills, and a different C expression can change its register
allocation in either direction. Binding plans report the selected branch/copy
lowerings with `backend_stack_bound: null` until assembly exists. Packaged MSX
projects report per-translation-unit helper bounds under
`stack_analysis.value_bindings`, including explicit unknown results. The scope
excludes caller-owned frames and argument publication. Callback measurements
in Edelweiss additionally cover the actual engine calls and continuation state.

Native statement expansion exposed another aggregate publication case. With
SDCC 4.2.0's default allocator settings, two results written by an expanded
boundary were copied to the next boundary incorrectly: a volatile load of the
first result clobbered A, then the second copy reused A instead of its loaded
second result. Host C and separate native functions agreed on the correct
values. A 2,000-attempt allocation cap avoided this particular reproducer but
is not itself a correctness proof. Z80 region emission therefore retains a
generated native function for offered expansions with multiple result leaves.
Scalar expansions remain direct; host C still expands aggregates. The plan
lists these target safeguards in `region_expansion_barriers`. The native
boundary oracle exercises aggregate reuse and reversed handoffs with separate
and shared objects and both fixed and automatic region frames.

The region emitter can now specialize the computation boundary itself for a
small predicate. A generated function reads the current owned fields and
returns their predicate without writing the frame or invoking native effects.
Since preceding stores remain outside this function, its input-only byte
arithmetic can retain the ordinary immutable-input lowering. Fixed frames
avoid scalar argument traffic; automatic frames pass a stable frame pointer.
This does not remove the boundary or volatile field safeguards. The initial
family excludes wide operands, selections and shared computed intermediates;
other predicates retain their previous helper calls. The predicate oracle
exercises wrapping, signed arithmetic, wide fallback and fresh native replies
with extraction disabled/enabled and both storage choices on Z80.

Finite value rules can now be realized as private constant tables in the same
ROM bank as their consumer. The solver exhausts all 256 patterns of a single
byte input, preserving signed interpretation and wrapping at every node. It
proves index compression and modular narrowing before replacing a subgraph.
For example, a word displacement in `[-120,120]` modulo 65536 can be stored as
signed bytes and converted back to the original unsigned word at the load.
This removes the entire comparison tree and its intermediate volatile fields;
it does not remove operand barriers from calculations that remain.

Publication and the real binding call boundary remain. Tables are immutable
internal values, not effectful memory reads, and their addresses never escape.
The finite-table oracle checks all byte inputs in automatic and fixed bindings,
including signed shifts/division, modular 32-bit values, guarded constant
suffixes and nested conditional results. It executes on Z80 with the game's
2,000-attempt allocator cap. ROM costs are reported separately from frame RAM;
the 8 KiB module checks include both code and constant tables.

The value emitter can now eliminate a shared selected temporary by branching
before its consumers. For a capped update, `spent = elapsed < room ? elapsed :
room`, the ordinary arm uses the original `elapsed` input directly. There is
no freshly stored `spent` for SDCC to forward incorrectly, so the addition and
comparisons in that arm no longer need computed-operand helpers. On the other
arm, the emitter retains helpers for computed values that still require stable
storage. The binding call boundary and volatile input/temporary safeguards are
unchanged.

The branch also carries exact ordering facts and same-width modular identities.
For example, when `room = (u8)(24-counter)`, consuming all of `room` makes the
new byte counter exactly 24. The emitter can publish that constant without an
addition helper. This identity is valid even when the subtraction wraps; it
does not assume a valid gameplay counter range.

The shared-choice oracle compares a bulk transition with a separate tick
interpreter for every pair of byte inputs on the host. Z80 executes the complete
counter domain at representative elapsed values, signed reinterpretations,
word wrapping, and both automatic and fixed binding layouts with 2,000 allocator
attempts per node. The existing stored-operand oracle also exercises the revised
clamp and coordinate compositions. These transformations stay within pure
blocks and preserve inactive-path scheduling; mutable effect regions retain
their previous lowering.

The shared-choice check now exercises budgets zero, two and eight, including a
nested clamp followed by subtraction and a three-step displacement. Disabling
splitting exposed another SDCC forwarding failure in the fixed frame: the
generated code loaded `room` into A, immediately loaded `spent` over it, then
subtracted the register holding `spent`. The published remainder became zero
even for `room=1, spent=0`. A real `VX_VALUE_SUB8` call protects dynamic byte
subtraction that consumes stored computations, just as ADD8 protects addition.
Pairs of original inputs, and subtraction with a constant operand, retain direct
arithmetic. Specialized paths
can eliminate the stored computation and therefore the helper entirely. Both
automatic and fixed frames execute the independent tick oracle with expansion
disabled and enabled; optimization is not required for correctness.

Joint predicate specialization extends the same approach to different values
that share a decision. For example, two coordinate displacements selected by
`elapsed == 0`, then `elapsed == 1`, can be computed together on each path.
The equality supplies a fact about the original elapsed input to the other
calculations too. The zero path can publish the original coordinates directly;
the one path can avoid both multiplication and stored displacement operands.
Computed operands remaining on other paths still receive their required
isolation helpers.

This heuristic requires multiple distinct selections feeding wide arithmetic,
each with at least one small alternative without nested decisions. It
shares the existing `value_choice_branches` allowance with selected-value
expansion, preserves invariant outputs, and operates only on unconditional
decisions in total pure graphs. It never infers an entire word from a comparison
of its low byte. The predicate oracle checks this distinction, signed velocities,
word/dword wrapping and all byte elapsed values against an independent step
interpreter on the host. Representative elapsed values and all byte velocities
also execute on Z80 with expansion disabled/enabled and automatic/fixed storage.

Runtime support is selected by the C emitter's actual helper uses. Generated
headers request independently guarded `vx_support_value_*` components, so SDCC
does not emit the complete arithmetic support in a module that uses only one function.
This removes unused definitions without changing helper calls, volatile fields,
or publication boundaries. The complete `vx_value_runtime.h` remains available
to handwritten C and can be included before or after generated headers. The
runtime-component regression checks include-order compatibility and the emitted
SDCC function symbols; the existing arithmetic oracles check execution.

Removing unused helpers exposed a previously layout-dependent operand-forwarding
failure in the legacy `vx_fixed_NAME.h` interface. Its `static inline` entry was
expanded into the client immediately after input publication. SDCC loaded the
first addend into A, overwrote it with the second, then added the second again.
The existing fixed-frame arithmetic oracle caught the error for `(0,3)`.
Standalone fixed entries now preserve a real computation function boundary, as
selected C bindings already do. This protects the public fixed-frame interface
regardless of which unrelated runtime helpers happen to precede its definition.

Modular products use their declared result width. The low eight or sixteen
product bits are independent of discarded high bits, including for signed
two's-complement operands. Generated `mul8`/`mul16` support therefore uses narrow
shift/add loops on Z80 instead of passing both operands through the 32-bit
`vx_mul` interface. These are real C functions, avoiding implicit SDCC library
dependencies; the host realization uses unsigned 32-bit multiplication before
narrowing, so integer promotion cannot introduce signed overflow. General
32-bit products keep the existing runtime call.

The two private product helpers use SDCC's per-function register convention
(`__sdcccall(1)`). Public entries, native boundaries and handwritten assembly
keep their established convention. An earlier experiment changing only the
byte helpers' convention while keeping volatile parameters increased time and
stack use in some cases. The current private `add8`, `sub8`, `lt8` and `add16`
leaves use register arguments with ordinary immutable scalar parameters.
Their bodies perform exactly one operation, and the real call boundary still
separates operand publication from computation. Volatile public parameters,
frame cells and snapshot reads retain their existing safeguards.

A constant power of two, or its modular negation, instead becomes an unsigned
shift and optional unsigned subtraction from zero. The dynamic operand is
evaluated once, and the original signedness and width are restored at the
result. Host shifts widen first where integer promotion otherwise risks overflow.

A constant represented by the sum or difference of two powers of two can use
one private unary helper. The helper receives the dynamic operand once, uses
unsigned shifts/addition/subtraction, and restores the original modular width
at its result. Negated sums are supported too. Only demanded helpers are
emitted, with a per-width/per-constant include guard so different generated
headers can share a definition. They use the register convention privately;
no external ABI changes. The host version uses unsigned multiplication.

`test_sparse_products.py` checks every byte input for representative sparse
coefficients, signed word boundaries and computed operands, including sums
whose full word result must survive. The measured combined workload takes
32.2% fewer ticks with automatic frames and 46.2% fewer with fixed frames than
the general narrow-product helpers; observed stack use is unchanged. These
measurements include the oracle and are not a claim about every coefficient.

`test_value_products.py` checks all byte operand pairs by repeated addition,
independent word-boundary vectors, and signed/unsigned constant products on the
host and Z80. In that combined workload, narrow products and constant shifts
first reduced simulated ticks by 51.8% with automatic frames and 52.8% with fixed
frames. Selecting the register convention only for the product helpers improves
the total reduction to 72.3% and 75.3%, respectively. Observed stack use falls
from 81 to 71 bytes and from 36 to 30 bytes respectively. These are
workload observations, not whole-game speed claims or worst-case stack bounds.

Finite-table and branch-combination tests exposed another forwarding failure:
after storing a selected byte and indexing a table with it, a word addition of
that byte and a second input could publish the selected byte twice. Passing
bytes instead of promoted words did not fix it. The preserved assembly showed
the second input loaded into A, then discarded while a previous register value
was reused for both arguments.

For a word sum whose operands are stable unsigned byte cells, `add8wide` now
borrows those cells privately and reads them inside the helper. This preserves
the full 0..510 result while avoiding the faulty publication sequence. The
borrow is confined to one synchronous operation on binding RAM or its automatic
frame; it adds no source pointer, retained address or ROM-bank dependency. The
emitter only selects it for actual lvalues and looks through widening casts,
never through a signed-byte conversion. Its macro therefore requires byte
lvalues, as the existing stable-copy macros do. Other word operands retain the
existing addition path. `test_finite_context.py` covers exhaustive byte pairs,
automatic/fixed storage, branch budgets zero/eight, and narrowing that must not
change the original wider input, on both the host and Z80.

Storage feasibility alone does not settle target cost. An experiment reusing
four bytes between mutually exclusive temporary lifetimes preserved all values,
but SDCC generated more spills for the resulting fixed frame: the test workload
took 26.5% more ticks and six extra stack bytes. That pooling change was rejected.
The current lifetime scheduler offers both reused and distinct pure temporary
cells through scoped `value_scratch_reuse` allowances. Measured search can retain
the distinct-cell alternative for a function whose smaller frame causes more
spills. The proof of disjoint lifetimes remains useful evidence; it is not
itself a speed estimate.

The same distinction applies to Boolean expressions. Replacing value selections
of zero/one by their equivalent predicates removed branch-sharing opportunities.
The checked workload became 12.4% slower with automatic frames and 8.0% slower
with fixed frames; the automatic form also used three more stack bytes. That
normalization was rejected. The emitter instead retains the physical selections
and carries known Boolean and comparison facts separately into their arms.

Moving every simplified neighboring result into a shared branch was also too
aggressive for fixed storage: one workload gained six stack bytes and 4.0% more
ticks. The selected frame policy keeps a previously independent result outside
the branch unless both alternatives simplify completely. The same oracle then
improves by 1.4% with automatic bindings and 0.8% with fixed bindings, with
unchanged observed stack. These figures describe those test workloads, not a
universal rule that branches or fixed storage are faster.

Optional case interfaces provide another choice. A case can carry invariant
outputs and independent result guards, specialize them under its established
predicate, and omit input fields that no longer contribute. The first selected
result determines the partition, exposed by the generated branch inspector.
A true numeric input retains its original nonzero value; it is not replaced by
one. The adapter must preserve one input snapshot across branch inspection and
case execution. Full and case interfaces remain alternatives, since reduced
input traffic need not imply lower target stack use.

## Known literals across conversions

A specialized operand may still contain several integer conversions. The
emitter now evaluates a constant through that complete conversion chain, from
the innermost conversion outward, and emits the resulting typed immediate.
An established branch choice can expose such a constant as well. This is
physical operand selection: the logical graph, case partitions, scheduled
effects and value snapshots remain intact.

The distinction matters for SDCC's forwarding safeguards. An operand that is
provably constant needs no second dynamic publication, even when its graph
node is a cast. Byte comparisons and byte/word arithmetic therefore retain
their barriers only for dynamic operand pairs. Adding or subtracting
a wrapped zero retains the result conversion and drops the arithmetic call.
Known multiplication coefficients also reach the existing shift and sparse
product choices through their casts. A coefficient that wraps to zero emits a
typed zero directly. Ordered region effects remain separate: an external call
whose result is multiplied by zero still executes.

`test_literal_operands.py` checks all word inputs on the host and representative
word/32-bit edges on Z80, with signed casts, both branch-specialization budgets,
automatic/fixed frames and an effectful region whose callback must still run
exactly once even when its result is discarded by a zero product. The broader
value and game oracles remain required: removing a
helper based only on its printed C operand would not establish these facts.

## Published operands and zero extension

The operand graph distinguishes an incoming parameter from a value published
by the current activation: a call result, loop snapshot or calculated temporary.
That origin survives interning and region reindexing. Byte comparisons and word
addition also follow published values through derived arithmetic. A real game
counterexample used two narrowed, shifted collision extents; looking only at
an immediate temporary missed the comparison that consumed them. The linked
helper probe found this in the earlier generated code, independently of the
new range-based storage choices.

Value provenance follows a selection's arms, rather than its predicate. A ROM
lookup produces a new value; its index is not the lookup result's provenance.
This distinction matters for cost: following every control dependency inserted
unnecessary barriers into otherwise independent constant arms. Byte arithmetic
in ordinary calculations additionally protects published call-result ancestry;
pure binding byte arithmetic retains its narrower direct-operand rule. These
are documented safeguards for observed SDCC behavior, not a formal model of
its optimizer.

The private `add8`, `sub8`, `lt8` and `add16` leaf helpers use register arguments
and immutable scalar parameters. Their call boundary still separates operand
publication from the arithmetic. Public Vexel/native interfaces, activation
storage and assembly adapters retain their existing ABI. The actual linked
helper oracle checks every selected full pure binding in Edelweiss over edge,
correlated and seeded input vectors; isolated timings describe that workload,
not a worst-case proof or the game's execution-frequency distribution.

Eliminating a shared temporary exposed another SDCC issue. Given a retained
signed byte whose bit pattern is `255`, the generated expression
`(uint32_t)(uint8_t)((uint16_t)(uint8_t)x << 1)` could produce `0xfffffffe`
instead of `254`. Its host C result was correct. Widening an unsigned logical
value now also states the zero extension with an explicit source-width mask.
The mask preserves the wrap even when the target optimizer forwards the
arithmetic through a signed register value. `test_value_ranges.py` retains this
case, word-width analogues and a wider expression consuming the result, with
automatic/fixed storage and both scratch-reuse choices.

The same requirement applies before an unsigned byte right shift. SDCC 4.2
can forward a wrapped left shift into an arithmetic right shift; simply casting
the operand to `uint16_t` can still sign-extend it. For a fixed count the emitter
uses `((uint8_t)operand >> count) & (255U >> count)`, including power-of-two
division. Clearing the result's high bits preserves byte instructions and can
combine with a consumer's mask. A proven-valid variable count instead uses
`((uint16_t)operand & 255U) >> count`, avoiding a second variable shift for the
result mask. The operators example exposed this at input
64, where a wrapped byte containing 128 was shifted right. The `shift_chain`
width fixture retains both shared intermediates and nested expressions, so a
temporary introduced by common-subexpression reuse cannot hide the failure.
An enclosing constant byte mask can provide this correction when it clears
every bit introduced by sign extension. The emitter carries that consumer
requirement only through unsigned byte-preserving casts. It stops at stored
producers and other arithmetic, so an independently observed full result keeps
its correction. This avoids SDCC emitting a redundant `res 7` before an
existing `and`; `masked_shifts` checks full and masked consumers of one value,
signed shifts, division, and masks that must not absorb the correction.

Mixed division/bitwise testing exposed the same operand forwarding failure in
`AND`: after publishing a remainder to fixed RAM, SDCC loaded the other operand
into A, overwrote A with that remainder, and emitted `r & r` for `d & r`.
The host result was correct; input `(1,1)` with `d=(b&63)+1` and `r=(a&31)%d`
returned 1 instead of 0 on the Z80. Dynamic byte `&`, `|`, and `^` now use the
private register-argument operand barrier when a value comes from a stored
calculation or mutable region. Constants and immutable input-only operations
retain direct code. `stored_bits` covers direct and derived published values,
signed interpretation, and automatic/fixed storage. The original mixed graph
and failing assembly are retained under
`tmp/eight-hour-20261120-0838/mixed-arithmetic-target/0-fixed`.
