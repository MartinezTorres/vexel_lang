# VEXEL - Language RFC (solver revision)

**Role**: Language vision and proposed semantics. This is not a claim that every
feature below is implemented. The executable's exact contract is the
[relational source contract](../experimental/relational/SOURCE.md), with a
[finite acceptance checklist](closure-checklist.md).
[architecture.md](architecture.md) describes the supported implementation.
The earlier `solver-profile.md` and `solver.md` describe historical code.

## Preamble

Vexel: strongly typed, minimal, operator-based language with no keywords.

**Release status**: experimental. The previous v1.0-rc1 freeze is retired.

**Solver revision, 2026-09-28**:

- Immutable globals are equations with dependencies, not a sequence of
  initialization commands. Forward references are permitted. Only demanded
  constants must resolve. Supported cyclic constraints are solved jointly;
  unanchored cycles remain open instead of acquiring a guessed final value.
- Unconstrained integer expressions retain exact mathematical integer values.
  The solver reports their internal type as `#integer` (not a source type).
  Concrete arithmetic wraps at its declared width; implicit concretization
  checks representability. Expected result types do not flow backwards into
  operands or select function overloads. See the profile for examples.
- Compile-time calculation and runtime residual operations use the same graph
  rules. External operations remain graph obligations; the solver never invokes
  them on the host.
- The operator precedence in the EBNF is authoritative. Multiplication binds
  more tightly than addition, addition than ranges, and ranges than shifts.
- Signed remainder and integer bitwise operations follow the coherent rules in
  the profile; earlier conflicting unsigned-only wording is superseded.
- Target facts can participate before source resolution is complete. Logical
  values, physical storage and access protocols are separate; no universal
  source pointer is required. See the [NEO-8 target](backend-msx-neo8.md).
- Resolution order must not determine meaning. Partial type knowledge,
  provisional choices, assumption-based search and revision of derived answers
  are part of the relational model below; no new keywords or sigils are introduced.

These changes preserve the language's sigil syntax, explicit external boundary,
value semantics, fixed-size data, and lack of language-level heap allocation.

**No keywords**: All syntax via sigils and operators. No reserved words. Primitive type names (`iN`, `uN`, `f16`, `f32`, `f64`, `b`, `s`) are only special after the `#` sigil; they may be used as identifiers elsewhere.

**No implicit platform library**: External functions provide I/O and platform
services. Explicitly imported bundled numeric modules provide portable pure
operations; project-local modules may replace them.

**No error handling**: Language provides no explicit error handling mechanism (no exceptions, result types, or error codes). Runtime errors trap and exit.

**Whole-program compilation**: All source available at compile time. Aggressive optimization and dead code elimination. Only explicitly exported declarations are callable/visible externally (`&^` functions, `^` globals).

**Compilation model**: Backend specifications define target language, type mappings, calling conventions, code organization. Compiler can generate executables, libraries, or C source for integration.

### Sources, sinks and activation overlap

Concurrency is described at graph boundaries. Sources and sinks have contracts
for their observations, causal relationships, allowed activation overlap and
completion. Threads and interrupts are implementation mechanisms of boundary
adapters; they introduce no hidden scheduling semantics into the language.

Reentrancy requirements follow from permitted overlapping activations, including
internal nested calls and recursion. A serial source does not by itself make
every helper's storage exclusive. An externally observable sink does not by
itself imply reentry. State shared across activations requires explicit state
and ordering relationships.

A realization may reuse distinct mutable storage only when its required
lifetimes do not overlap. Unknown overlap is not evidence of exclusion, and a
result reaching a sink need not end the lifetime of all work or storage involved.
The [activation contract](activation-contracts.md) develops these rules without
adding source keywords. Its finite relational realization distinguishes source
definitions from named activation occurrences, and execution completion from
result release. Explicit begin/produce/release relationships constrain both
storage and access ordering, including repeated activations of one source.
The boundary adapter owns these occurrences and must honor their finite bounds.
Repeated streams can instead use explicit bounded result pools with checked
leases, serial execution and backpressure, as described under annotations.
General inference of event-stream bounds and ownership remains open;
implementation coverage is tracked in the [completion ledger](relational-completion.md).

## Resolution, Assumptions & Order Independence

**Resolution is a property of the whole relevant puzzle, not of the first
answer computed.** Source declarations, imports, type relationships, demanded
values and target requirements may constrain one another. A solver may revisit
an earlier answer when another piece becomes available. "Already computed"
does not mean "established without assumptions."

For the same source/resource snapshots, requested roots, target inputs and
language policies, changing discovery order, worklist order or search order
must not change source meaning. Reordering independent global declarations or
imports must not select a different overload or infer a different value. This
does not make runtime statements interchangeable: evaluation order, mutation,
traps and external effects retain their specified semantics.

Different searches may produce different valid machine implementations. If
semantic alternatives remain distinguishable at a demanded boundary, finding
one working alternative is insufficient: the language must determine the
answer, or resolution reports ambiguity. Budget exhaustion or an unsupported
constraint is reported as inability to finish resolution, not proof that no
solution exists. The language does not promise a complete solver for arbitrary
programs.

### Knowledge, assumptions and choices

The resolver distinguishes:

- **Established relationships**: requirements justified by the program or
  target contract, such as an annotation, a field access or an array dimension.
- **Partial answers**: some properties are known while others remain open.
  Knowing an array's length need not settle its element width or layout.
- **Conditional answers**: conclusions with explicit assumptions, such as a
  value obtained if an operand has a particular width.
- **Candidates**: possible choices being explored, including overloads,
  specializations and physical layouts. Choosing a candidate for exploration
  does not establish it as the program's meaning.

Later information may eliminate candidates, change a provisional answer,
reactivate dependencies or require backtracking. Every derived answer records
the facts and assumptions supporting it. Rejecting an assumption invalidates
its dependent answers, including generated code and measurements; independent
answers remain usable. A speculative conflict rejects that branch, not the
whole program unless all admissible alternatives have been excluded.

Monotonic knowledge is compatible with this behavior: learning more narrows
the set of possible complete solutions, while the current candidate can change
entirely. "Resolved" must identify a property, an input context and any remaining
assumptions. An unconditional answer is justified across all admissible
completions of that context, rather than merely across the candidates discovered
so far. This is a soundness requirement, not an instruction to enumerate every
completion. A conditional region may be compiled or cached earlier, provided
its assumptions accompany it and are discharged before use as a final artifact.
A contradictory context cannot justify emission, even if a particular result
could be claimed vacuously after all possible solutions have been excluded.

A changed source file, resource or target setting creates a new input revision.
Previously established answers may then be invalidated and recomputed. There is
no promise that a fact survives changes to its premises.

### Incremental reuse

An implementation may retain a closed answer across revisions when its complete
support still holds. Support includes the relevant closed declaration sets,
source and resource contents, directory membership when observed, path selection
(including absent preferred paths), requested observations and target/boundary
contracts. A cached negative capability depends on the closed signature set as
well as the argument properties used to establish it.

Reuse must preserve the meaning of a fresh solve. Changed support invalidates
the answer and its generated projection; an unsuccessful new solve cannot emit
the previous revision's projection as if it were current. Unknown, ambiguous or
exhausted outcomes cannot become established facts merely through caching.
Measurements retain their own artifact and toolchain premises: reusing generated
C does not establish that an old ROM measurement applies to a newly linked ROM.

Dependency granularity is an implementation choice. The current implementation
retains whole resolved requests in a live workspace. Each group depends on its
full parsed import closure and the resources actually observed by that request;
changing any source in that closure conservatively rebuilds the group. Unrelated
resource groups can remain reusable. Process-dependent answers are recomputed
in each new request revision because process inputs are not declared completely.
This mechanism does not require mutable declaration membership, fine-grained
solver-state rollback, or a new language phase.

### Late type information

For example, the existing numeric rules give this result:

```vexel
^answer:#u16 = left + right;
left:#u8 = 250;
right:#u8 = 10;
```

`answer` is 4: the operands establish eight-bit arithmetic, then the result
widens to sixteen bits. Discovering the destination first must not produce 260.
Discovering literal initializers before their annotations must not prematurely
establish an exact-integer addition. The solver waits for relevant operand
properties, carries a relationship, or computes under an explicit assumption.
Every permutation of these declarations has the same meaning.

In contrast, `^answer:#u16 = 250 + 10;` is 260 under the current exact-integer
rules. The restriction on expected result types flowing backwards into operands
is a language policy, not a consequence of worklist direction or immutable
caches. A future contextual-typing revision could permit that relationship,
provided it specifies arithmetic and ambiguity consistently for every discovery
order. Physical storage selection alone cannot introduce wrapping that the
logical operation did not specify.

### Closing a demanded question

A final answer must account for every dependency that can affect that question,
including the completeness of relevant declaration and overload sets. Finding
one applicable overload while an applicable import is still unresolved does
not establish the winner. Not yet finding a member does not establish its
absence. A probe must remain pending until its positive or negative answer is
justified; it must not select a fallback merely because that path was ready first.

Closure is local to a question. Establishing a call signature need not evaluate
the function body; establishing an array shape need not allocate its storage.
Dormant code remains undemanded where the language permits it. Cyclic constraints
may be solved jointly when the supported rules establish an answer; a cycle by
itself neither licenses a guess nor establishes runtime dependence. Incomplete
resolution must expose the remaining obligations.

For example, type/shape questions may establish `a:#u8[|b|+1]` and
`b:#u8[7-|a|]` as lengths 4 and 3 without either array having an initializer.
This establishes their shapes, not initialized element values. A later element
read still needs initialization. The independent
[relational frontend](../experimental/relational/SOURCE.md) implements this
linear case; that does not imply support for every cyclic inference family.

For a conditional aggregate with the same outer structure on both branches,
field types, array element types, array lengths and captured schema identity
are separate questions. A shared field type or array length does not require
the condition's value or unrelated fields. If the requested property differs
between branches, the condition is a real dependency. Whole-interface and
value demands still require their remaining properties and ordinary control
flow. Exits cannot be discarded by a property projection.

```vexel
#Pair(x,y);
^answer:#u8 = {
    c:#b;
    v:#[(c?#Pair((#u8)1,missing()):#Pair((#u8)2,missing())).x]=7;
    v // Both branches establish x's type; c and y are not needed.
};
```

The common element type of an array has the same separation. Nested lengths
can be established without knowing the leaf element types; a record field can
join the corresponding fields of all elements without demanding their other
fields. Every element still contributes to the requested common property.
Whole-interface queries and value execution close the remaining type, schema,
completion and effect obligations.

Target planning follows the same rules. A provisional placement of a function
in one NEO-8 bank may fail when SDCC reveals that the function and required
helpers exceed 8 KiB. The solver may try another placement, partition or code
strategy, revising all affected projections. It must preserve the function's
logical behavior and discharge the new plan's storage and access obligations.
No globally optimal placement is required. An arbitrary physical choice may
not silently select different observable source behavior; any target property
used to select such behavior needs a declared input or language-defined policy.

### Optional LLM-guided search

An LLM may guide difficult decisions throughout resolution. It can suggest which
constraint to branch on, rank candidate types or representations, propose a
decomposition, choose a pure subproblem to compile, or suggest a different bank
and storage plan. This is an optional search mechanism, not a source-language
feature; no keyword, model service or particular provider is required.

Each proposal becomes an explicit candidate with dependencies and assumptions.
The resolver derives its consequences and checks its obligations. Later
information may reject it and trigger backtracking, including invalidation of
dependent compiled regions. A failure can be supplied to the LLM to improve its
next proposal. Unsupported, malformed or stale proposals may be declined.

The model's confidence cannot establish a fact, discharge an assumption or
resolve semantic ambiguity. Candidate types or overloads may be explored in a
model-suggested order, but final selection still obeys the language rules.
Suggestions to rewrite a computation require an established transformation rule
or adequate equivalence evidence; compiling successfully or passing a few inputs
does not by itself establish equivalence.

The pure semantic system remains defined independently of the proposal source.
Live model replies are external search inputs, recorded with their request
context and the solver's decisions for replay without another model call.
Search paths, costs and valid physical artifacts may differ between live runs;
the meaning of a successfully resolved source program must not. Disabling the
model leaves ordinary search available, subject to its own supported domains
and budgets. A model timeout or a search budget limit is not proof of
infeasibility. The executable supports advice for finite numeric decisions and
finite structural alternatives, sharing a per-puzzle request budget across
revisions and recording rejected attempts for offline replay. The
[provider protocol](search-provider.md) specifies this bounded interface.
Broader integration is proposed in
[realization evidence](realization-evidence.md#optional-llm-search-provider).

### Explicit finite solving

The executable experiment adds three prefix uses of the existing `?` token:

```vexel
^answer:#u8 = ?{
    x = ?(#u8, 1..10);
    y = ?(#u8, 1..10);
    ?(x + y == 10);
    ?(x * y == 21);
    ?(x < y);
    x
};
```

`?{ ... }` closes a pure finite constraint region and yields its final expression.
`?(#T, domain)` introduces a fresh choice from a constant array of admissible
values. For a fixed array type, each scalar leaf is a separate choice from that
domain. `?(predicate)` requires a Boolean predicate to hold on the current
execution path. These forms introduce no reserved words and are distinct from
ordinary uninitialized storage and runtime input. The former transactional
`#{ ... }` spelling is no longer part of the language.

Ordinary arithmetic in the region supplies relations with the same exact and
concrete-width semantics as other Vexel arithmetic. Calls, copies and local
updates retain their usual behavior. A conditional requirement is guarded by
its execution condition; inactive paths do not impose requirements or traps.
Invalid arithmetic excludes the corresponding candidate evaluation. Parse
errors, unfinished proofs and exhausted implementation budgets are not proofs
of inconsistency.

Success requires a satisfying assignment and a uniquely determined observable
result. Different assignments to unobserved internal choices are permitted.
Two distinct result values constitute ambiguity, rather than permission to
return whichever search finds first. A separate target-planning policy may
choose among valid physical realizations without imposing source ambiguity.

The initial domain representation uses integers and Booleans and finite tables.
It does not make arbitrary symbolic programs, infinite domains, dynamic shapes
or all existing RFC features implemented by implication. The authoritative
executable boundary remains the solver profile. `examples/sudoku.vx` expresses
row, column and box constraints using the ordinary array and iteration syntax;
there is no Sudoku-specific operation in the resolver.

### Proposed conformance cases

These extend the existing schedule checks and specify future implementation
work, rather than claiming these capabilities already exist:

- Permute independent declarations, imports and fact delivery; demand the same
  roots in different orders. Results and resolved interfaces must agree.
- Obtain a shape before an element width, then supply the width. Existing shape
  knowledge survives and all width-dependent obligations are resolved correctly.
- Reject an assumption after several dependent answers have been derived.
  Compare the surviving result with a fresh solve containing all final inputs.
- Delay an overload or member dependency. Neither an early winner nor an early
  negative probe may escape as an unconditional answer.
- Reject a bank-placement candidate after size measurement. Stale code, layout
  facts and evidence must not survive into the replacement artifact.
- Supply contradictory, stale or invalid model proposals, then disable the
  provider. Supported search must remain sound; exhaustion must not become a
  false impossibility claim. Replay a recorded search without contacting a model.

---

## Lexical & Tokens

- **ASCII-7 only**. Whitespace: space, tab, CR, LF.
- **Identifiers**: `[A-Za-z_][A-Za-z0-9_]*` (no length limit)
- **Comments**: `//` to end-of-line
- **Literals**:
  - Integers: `123`, `0xFF`, `0XFF`. Parsed as exact unresolved signed/unsigned integer literals (no host-width overflow during lexing/parsing); concrete integer types are inferred from surrounding constraints.
  - Floats: `1.23`, `1.23e4`, `1.23E-4`. Default: `#f64`
  - Strings: `"..."` with escapes: `\n` `\r` `\t` `\\` `\"` `\xHH` (hex) `\N`, `\NN`, or `\NNN` (octal), no length limit
  - Chars: `'a'`. Default: `#u8`
- **Sigils**: `$` (expression param), `@` (iteration / unrolled loop), `&` (inline function decl), `&~` (non-inlined function decl), `&!` (external function), `&^` (exported function), `^` (exported global), `!` (external global symbol), `!!` (backend-bound global), `#` (type)
- **Operators**: `+ - * / % & | ^ ~ << >> == != < <= > >= = -> ->| ->> . , ; : :: @ @@ !@ ! && || ? ( ) { } [ ]` plus dotted per-element binary and compound-assignment variants (for example `.+`, `.*`, `.==`, `.&&`, `.<<`, `.+=`, `.||=`). User types may overload scalar and dotted operator names via operator methods.
  - Note: `|` serves as both unary (length/absolute) and binary (bitwise OR) operator
- **Statement termination**: `;` or any whitespace when unambiguous (see Section 8 for ambiguous parsing rules)
- **Annotations**: Optional prefixes `[[name]]` or `[[name(arg1, arg2)]]` may precede declarations, statements, and expressions. Annotations are opaque to language semantics; the frontend preserves them for backends/tooling.

---

## Types

**Primitives**:
- Parametric signed integers: `#iN` where `N` is any positive integer width
- Parametric unsigned integers: `#uN` where `N` is any positive integer width
- Fixed-point syntax: `#iI.F`, `#uI.F` where `I + F > 0` (signed form counts the sign bit inside `I`)
  - Operands must match type exactly for fixed-point operators (no implicit fixed/fixed rescaling)
  - Supported operators:
    - unary: `+`, `-`, `~`
    - binary: `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `<<`, `>>`, `==`, `!=`, `<`, `<=`, `>`, `>=`
    - compound: `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`
  - Fixed-point `&&`, `||`, `&&=`, `||=` are not defined
  - Bitwise/shift semantics are raw-bit operations over the stored fixed representation; signed right shift is arithmetic
  - A stored integer `r` denotes `r / 2^F`. Arithmetic wraps to `I+F`
    storage bits. Multiplication and division compute a full intermediate,
    rescale to the result's fraction, and truncate toward zero before wrapping.
    Remainder uses the truncated quotient, with the sign of the dividend.
    Division by zero and out-of-range shift counts are errors or runtime traps.
  - Shift counts are integers. Unresolved integer literals may concretize to
    fixed point when representable; already typed integer and fixed values
    require an explicit cast between families. Fixed-to-fixed casts rescale,
    truncating toward zero when removing fraction bits, then wrap to the target.
    Signed fixed point requires `I >= 1`; unsigned `I = 0` is supported.
  - `* / %` and compound forms are supported for:
    - any non-native storage width (extint path), and
    - native storage widths `8/16/32/64`
  - Explicit casts are supported among fixed/integer/bool/float primitives and `#u8[...]` byte-array casts
- Floating point: `#f16`, `#f32`, `#f64`
  - Binary IEEE formats use round-to-nearest, ties-to-even, with gradual
    underflow. Each arithmetic operation rounds to its result format.
  - Arithmetic supports `+`, `-`, `*`, `/`, `%` and absolute value. `%` is
    the remainder after a quotient truncated toward zero. Bitwise and shift
    operators require integer or fixed-point operands.
  - Division by zero produces signed infinity (or NaN for zero divided by
    zero). Invalid floating operations produce a canonical positive quiet NaN.
    Comparisons with NaN are false except `!=`; sorted iteration rejects an
    unordered comparison. Signed zeros compare equal and retain distinct bits.
  - Explicit numeric casts to integer or fixed point truncate toward zero and
    wrap to the target width. Non-finite inputs have no such representation
    and cause an error or runtime trap. Explicit Boolean casts test nonzero;
    NaN converts to 1. Float-to-float narrowing rounds to the target format.
  - An unresolved integer literal implicitly concretizes to a float only if
    exactly representable. Use an explicit cast to request rounding otherwise.
    Integer/fixed/float family changes otherwise require an explicit cast.
  - Canonical byte casts preserve the IEEE bits, including signed zeros and
    NaN payloads. Arithmetic canonicalizes NaNs; byte copies do not.
- Backends may support only a subset of integer widths (for example, some targets may accept only 8/16/32/64)
- Boolean: `#b` (values 0 or 1 only, no true/false literals)
  - Use 0 for false, 1 for true
  - `#b[N]` is semantically an array of N logical bits; backends may choose packed or unpacked physical storage
- String: `#s` (immutable, compile-time constant)
  - String length: `|str|` yields compile-time constant integer (number of bytes)

**Arrays**: `#T[N]` where N is a compile-time known non-negative integer.
- Size must be a non-negative integer resolvable at compile time
- Cannot depend on external calls, runtime values, or I/O
- Examples: `#i32[10]`, `#u8[2+3]`, `#f32[pow(2,3)]` (if pow is pure)
- Array length: `|arr|` yields compile-time constant integer (array size)
- Literal `[e1,e2,...]` infers size from element count (may be empty)
  - `[ ]` produces `#T[0]` for the target type `#T` when context requires an array
  - Element type is inferred by unifying element expression types
  - If all elements are unresolved integer literals, element type remains unresolved until constrained by context
  - Once a concrete element type is known, each element must be representable in that type
- Size is part of type: `#i32[10]` != `#i32[20]`

**Type constructors**: `#Name(field[:Type],...);`
- Defines record type `#Name`
- Auto-generates constructor callable as `#Name(args...)`
- Fields accessed via `.field`
- Can be defined at module level or within any scope
- **Cannot be recursive** (no self-reference in fields, no pointers)

**Generics**: Omitted param/field types become type parameters; monomorphized at use sites.

**Dependent fields**: All fields of a record are in scope in its field
annotations, including fields declared later. An annotation may use another
field's type or relate an array dimension to a field value:

```vexel
#Packet(count:#u8, data:#u8[count]);
#Sample(value:#[reference], reference:#u16);
```

Construction binds all argument projections before resolving these annotations.
An array argument supplies its fixed shape; `data:#u8[count]` then requires
`count` to equal that shape's extent. A known mismatch is an error, an explicit
finite choice receives a constraint, and a runtime count receives a checked
condition. This does not introduce runtime-sized storage: the array's physical
shape is already established by the argument's type. A bare `#Packet` is an open
requirement; a concrete instance supplies the missing field shapes.

Assignments preserve these relationships. Changing a dimension field must still
agree with the existing array shape; changing layout requires constructing a
new compatible value rather than resizing an existing location. Representation
casts must also satisfy the relationships. Type queries resolve field types
without executing these value checks. Dimension expressions cannot perform
external operations or modify enclosing storage. A helper may mutate its own
parameters and scratch locals. Constructor arguments may have effects and
evaluate once, in ordinary argument order.

Lexical values captured by a dependent record invariant are compile-time schema
parameters, frozen when the type is instantiated. Their types and values are
part of that schema's identity: two equal field layouts with different captured
constraints are not interchangeable. Later assignments to the original local
do not change an existing record's invariant. A runtime dependency belongs in
an explicit field, for example
`#Packet(offset:#u8,count:#u8,data:#u8[count+offset]);`. This keeps runtime
dependencies owned by the record and copied with it; a type annotation cannot
silently retain a reference into an expired activation or replay its operations.
Capture requirements follow applicable overloads. An unrelated overload's body
does not impose a capture requirement once argument types establish the selected
body; declaration order does not establish that selection.
A bare generic record parameter is instantiated for each application, using the
argument's field interfaces to resolve invariant dispatch. Nested record, tuple
and array requirements retain this context. One call cannot specialize another
call's parameter requirement. Schema captures still come from the annotation's
lexical environment and reaching state; matching field layouts do not make
different frozen constraints interchangeable. A different nominal family can be
rejected before its unrelated schema captures are demanded.
Initialized bindings, casts and return annotations instantiate open record
requirements from their supplied interfaces in the same way. A function with
early returns applies its annotation to the returning paths and their resumed
lexical state. No return path contributes a value from an exited continuation.
Recursive return interfaces may use a base path as a candidate, but the complete
return equation and captured constraints must validate that candidate before
the demanded result is accepted.
A result interface can also be established by proving that it is independent
of the supplied values. For example, `&f(flag:#b){flag?{->(#u8)1;};(#u8)2}`
has result type `#u8` even when the Boolean argument has no known value.
Such a proof contributes only the type: it neither initializes the argument
nor performs any source or sink operation. It must account for every reachable
return and ordinary completion. If branches have different result types, the
condition remains a dependency; failure of a value-independent proof does not
invalidate a later concrete invocation. Dependent dimensions and frozen schema
captures remain part of the result interface and must have their own evidence.
This includes implicit calls made by operators, compound assignment, scalar
broadcasting and iteration. A builtin fallback does not inherit the captures
of an inapplicable method. Captured writes through those calls must likewise
participate in recursive state relationships; materialization cannot preserve
a value as constant when a recursive activation can change its logical slot.
Field layout and captured schema identity are separate properties. A query of
a scalar field's type, or an argument-supplied array shape, need not close an
unrelated captured invariant. Using a complete record type or converting a
record closes the required schema obligations against their original input
context. An unread capture is not a known empty schema.

A component made entirely of direct field-type aliases, such as
`#Pair(a:#[b], b:#[a]);`, joins all supplied argument types before fixing either
field's type. It uses the same common-type rules as array elements; it neither
guesses a width nor widens a concrete type just to fit an exact literal.
An explicit annotation anchoring a component retains its stated restriction.

The same policy applies to direct global type-alias components within a lexical
declaration group. Imports complete that group before resolution. For example,
`a:#[b]=(#u8)255; b:#[a]=(#u16)2;` gives both bindings `#u16`, independently of
declaration and demand order. Joining initializer types does not evaluate their
values. Primitive annotations and unannotated declarations remain anchors under
the usual rules. A nested group's references to outer bindings consume the
outer group's established interface. A component without any type witness
remains unresolved; this policy supplies no value for a genuine value cycle.

Initialized globals and constructed record fields also support query equations
beyond direct aliases:

```vexel
a:#[b+1] = (#u8)255;
b:#[a+1] = (#u16)2;
^answer:#u16 = a+1; // 256: both equations establish #u16.

#Pair(left:#[right+1], right:#[left+1]);
^field:#u16 = { p=#Pair((#u8)255,(#u16)2); p.left+1 };
```

Cyclic inference derives a domain of proposed interfaces for each demanded
property. Initializer and argument interfaces supply starting proposals. In
isolated contexts, ordinary source relationships evaluate the witnesses and
original queries under combinations of the proposals already available. Any
new result interface, and any compatible structural common type, joins the
domain. The resolver repeats until it proves that no new proposal can be
produced. The domains grow; a selected type is never irreversibly widened.

The closed domains become choices in the owning solving request. Each choice
must equal its original query exactly and admit its initializer interface under
the common-type rules. Values still need their ordinary conversion checks when
demanded. Overload selection, other consumers, schema obligations and backend
constraints participate in this same request; a private type search cannot
publish an arbitrary winner before those constraints are known.

For example, these equations have two valid interface assignments:

```vexel
&flip(x:#u8)->#u16 {0}
&flip(x:#u16)->#u8 {0}
a:#[flip(b)] = (#u8)0;
b:#[flip(a)] = (#u8)0;
```

The assignments are `(a:#u8,b:#u16)` and `(a:#u16,b:#u8)`. Asking for `a`'s
type is ambiguous. Asking for the value and type of `a+b` establishes zero and
`#u16`, because both assignments agree on those observations. An ordinary
consumer requiring `a` to convert implicitly to `#u8` rejects the second
assignment. Giving `b` a `#u16` initializer likewise selects the first. The
self-equation `a:#[flip(a)]=(#u8)0` is contradictory. These conclusions must be
independent of declaration, demand, worklist and branch order.

Proposal discovery does not mistake an ambiguous provisional overload for a
completed type model, and continues examining its sibling contexts. The owning
request still checks overload specificity normally. An unfinished alternative
cannot establish domain closure. A request needing facts from an enclosing
execution suspends for those named inputs and retries with explicit evidence.
A growing domain or an unfinished search may exhaust its budget; that outcome
is distinct from contradiction and ambiguity. No proposal invents a value or
executes an initializer merely to learn its independent interface.

The executable rule covers settled scalar, array, tuple and record witnesses for
globals, including scoped imports, and constructed record fields. For example:

```vexel
#Box(value);
#Pair(left:#[#Box(right.value+1)], right:#[#Box(left.value+1)]);
^nested:#u16 = {
    p=#Pair(#Box((#u8)255),#Box((#u16)2));
    p.left.value+1 // 256; both inner values have type #u16.
};
```

Each module instance and constructor keeps its own lexical environment. A
constructor field does not borrow a candidate from another use of the same
record declaration. Record candidates retain nominal identity, fixed field
requirements and frozen schema parameters. Known subproperties remain available
without solving unused siblings. Pending field obligations are not generic
holes that an initializer may silently replace. General inference of unresolved
schema environments still requires work.
The current executable computes this closure of constructively supplied
interfaces. It searches nonmonotone relationships within that closed domain;
it does not enumerate every possible type or infer arbitrary missing schema
environments. A component with no derivable interface remains unresolved.
General bidirectional discovery from unanchored overload signatures remains an
implementation requirement, not a claim made by this finite-domain rule.

Transforming equations also admit independent property proofs. For example:

```vexel
^answer:#u16 = {
    unknown:#u8;
    #Box(x,data:#u8[x+unknown]);
    #Pair(a:#[#Box(b.x+1,b.data)],b:#[#Box(a.x+1,a.data)]);
    v:#[#Pair(#Box((#u8)2,[1,2,3]),
              #Box((#u16)2,[1,2,3])).a.x]=300;
    v
};
```

The requested `x` interfaces share a candidate vector and are checked against
their original query projections. The pending captured schema is retained.
This query returns 300; a whole-record query or value demand still reads
`unknown` and is rejected. Tuple fields and nested record/array paths follow
the same rule. A tuple's outer shape does not demand every field's type.

Initializer witnesses remain constraints throughout candidate revision. If
selecting an overload exposes a previously unused field equation, that field's
witness joins the proof before publication, even when its type became concrete
under an assumption. Witness interfaces that themselves depend on candidates
are rechecked on every revision. Every surviving assignment must exactly
satisfy the original queries and admit its witnesses. This mechanism supplies
no missing values and discharges no unrelated schema or conversion obligation.

Direct `#[name]` alias components establish their common type property by
property. A query of `a.x` need not close an unrelated field or schema in the
same component. All supplied interfaces contribute to the queried property;
unused properties retain their obligations and are checked when demanded.
This independence also applies to aliases of nested arrays and tuples.

Type equality can also be proved through an ordinary helper, rather than inferred
from the spelling `#[name]`. For example:

```vexel
^schema_independent:#u16 = {
    unknown:#u8;
    #Captured(x,data:#u8[x+unknown]);
    &forward(item){item}
    #Pair(a:#[forward(b)],b:#[forward(a)]);
    v:#[#Pair(#Captured((#u8)2,[1,2,3]),
              #Captured((#u16)2,[1,2,3])).a.x]=300;
    v
};
```

Here the resolver can prove `type(forward(item)) = type(item)` without knowing
what `item` contains. The resulting closed equality component has the same
property-wise join as direct aliases. It establishes `x:#u16` while retaining
the obligation to read `unknown` if the whole record schema is later needed.
Neither helper names nor a particular return-expression spelling carry special
meaning. Nested helpers, expression parameters and tuple projections can carry
the same type equality.

This proof uses opaque type variables in an isolated graph. It must establish
an equality without choosing a representation or supplying a missing value.
If overload selection, a structural probe, a conversion or a value-dependent
result needs more information, the proof remains inconclusive and ordinary
resolution continues. Only a closed component in one declaration group can use
all its initializer witnesses this way; a nested group cannot revise its
parent's types. This extends the delivered equality rule, not the general
ability to infer arbitrary schemas through transforming equations.

Explicit array dimensions can also form checked equations:

```vexel
a:#u8[|b|] = [1,2,3];
b:#u8[|a|] = [4,5,6];
^answer:#u8 = a[0]+b[0]; // 5, independent of declaration order.
```

Here initializers supply candidate extents, while the annotations retain their
element requirements. The original dimension expressions must validate those
extents before they become established. Nested dimensions and arrays of generic
records follow the same rule. Inferring an extent does not evaluate leaf values,
erase a fixed field type, relax a conversion or waive a record invariant.
Incompatible shapes remain conflicts; unsupported or unanchored relationships
remain unresolved. This rule does not enumerate arbitrary array lengths.

When an equation requires a finite choice, the owning solve region explores
that choice with the same rollback and observable-uniqueness rules as ordinary
expressions. An invalid equation on a symbolic guarded branch rejects that
branch. An exhausted search never establishes that the branch is impossible.

**Partial type knowledge**: Logical type properties may resolve independently.
A record's named identity, its available fields and the types of those fields
need not all become known together. Likewise, an array's length and element
type are separate obligations. These relationships are internal solver objects;
they introduce no source keywords or additional type constructors.

Existing syntax expresses both open requirements and explicit restrictions:

```vexel
#Pair(left, right);
#Pixel(x:#u8, y:#u8);
&first(p) { p.left }
```

`Pair` leaves its field types to each instantiation. `Pixel` constrains both
fields. `first` requires a resolvable `left` field and relates its result type
to that field's type, without requiring a particular record name or physical
layout. Each use must ultimately satisfy those requirements. Named records
retain nominal identity; generic field requirements do not make distinct named
types equal or introduce implicit conversions between them.

**No function types**: Functions cannot be stored in variables or passed as values. Function references only exist through direct calls or generic instantiation.

**Type annotation**: `x:#T`
- Shorthand `x #T` is not valid; `:` is required.

**Type expressions**: `#[expr]`
- Resolves to the static type of `expr` in the current scope.
- Uses the same inference state as normal expression typing (including unresolved integer literal types until constrained).
- `expr` is type-checked only; `#[expr]` does not evaluate `expr` or trigger side effects.
- May relate partially known types; the consuming operation determines which properties must become concrete.
- Type computation may demand pure compile-time values used in dimensions or type-selecting conditions. It must not execute the queried expression's runtime operations or call external functions to discover a type. A signature can supply type information without supplying a value.
- Valid anywhere a type is expected, including cast targets and declaration annotations.

**Concreteness requirements**:
- Concreteness is required per demanded property, rather than for an entire type at every use.
- Primitive integer types with unknown width (`#i?` / `#u?`, descriptive notation) have an unresolved width. Their other established properties remain usable.
- A shape query requires the requested shape. An operation whose meaning depends on integer width requires that width, or a conditional relationship that will be discharged before use.
- Exact compile-time integer calculations do not need a machine width. Physical storage is unnecessary for values eliminated during resolution.
- Unresolved properties are allowed where no demanded operation needs them, including dead code. This does not excuse an unresolved effect or completion obligation on an executed path.
- ABI boundaries (`&^`, `&!`, exported globals) require concrete ABI-safe types.
- Final emission requires all properties needed by the chosen realization, including storage, access and calling obligations. Speculative projections must retain their assumptions and cannot be mistaken for final output.

**Comparison semantics**:
- All values support `<`, `>`, `<=`, `>=`, `==`, and `!=`.
- Primitives (`#i*`, `#u*`, `#f*`, `#b`, `#s`) use numeric or lexicographic ordering.
- Arrays compare lexicographically by element; mismatched lengths compare by prefix.
- Composite types compare by fields in declaration order.
- Operator methods (e.g., `&(lhs)#Vec2::==(rhs)`) override the default comparison when defined.

**Casts**: `(#T)expr` (explicit conversion, always creates a copy)
- **All casts create copies**: Cast result is a new value, never a reference
  - Modifying cast result doesn't affect original
  - Backend can optimize copy away when safe
- **Boolean array casts**: `#b[N]` can cast to/from `#uN` when bit sizes match exactly
  - Size mismatch is compile-time error
  - Bit 0 is LSB, bit N-1 is MSB
- **Composite type casts**: Can cast between composites and byte arrays
  - Creates byte representation (portable across backends)
  - Size must match exactly in bytes
  - Backend handles layout conversion transparently
  - The canonical representation is little-endian and contains no alignment
    padding. Each integer occupies `ceil(N/8)` bytes, holds its N two's-complement
    bits, and has zero unused high bits when encoded. Fixed point uses the same
    representation for its stored scaled integer. Decoding discards unused
    high bits. A Boolean occupies one byte; decoding a value other than 0 or 1
    is an error at compile time or a runtime trap.
  - Arrays concatenate element representations; records and tuples concatenate
    fields in declaration order. Empty composites occupy zero canonical bytes.
    Boolean arrays use one byte per element in this byte representation; their
    separate integer cast packs one logical bit per element as specified above.
  - Exact integers have no byte representation until given a concrete width.
    Casting an untyped integer array to a typed array first concretizes its
    elements; it does not invent an integer layout.

**Automatic promotion**: Numeric operations widen within the same family to the larger bit width:
- `#iA` with `#iB` promotes to `#i(max(A,B))`
- `#uA` with `#uB` promotes to `#u(max(A,B))`
- `#f16`/`#f32`/`#f64` promote to the wider float type
- No implicit narrowing
- No implicit signed/unsigned conversion (must use explicit cast)

**No void/unit type**: Functions without `->` infer return type when their body has a final expression. Empty blocks (no final expression), empty return `->;`, and statement conditionals have no type (not expressions). A function with no `->` and no final expression returns nothing (backend maps this to `void` where applicable).

**Memory model**: No language-level heap allocation. Logical lifetime and value
semantics do not require a particular physical stack placement. Registers,
fixed RAM, proven non-overlapping storage, ROM and target-bound resources are
possible realizations, subject to access, lifetime and reentrancy constraints.

**Integer literal typing**:
- Integer literals start as exact integer carriers (reported as `#integer` by the solver).
- Their numeric value is tracked exactly until concretization (no implicit truncation to host integer widths).
- Concretization happens at typed uses, casts, or concrete operator partners. A destination type does not flow backwards through an arithmetic expression or choose a call target.
- Representability checks are strict at concretization time; non-representable literals are compile-time errors.
- Boolean contexts accept only integer literals representable as `#b` (0 or 1).
- Reference compiler strictness modes:
  - level 0 (default): relaxed unresolved literal flow
  - level 1: explicit type annotation required for newly-declared variables
  - level 2: additionally rejects unresolved literal flow across unconstrained call boundaries
- Floating literals: `#f64`
- Character literals: `#u8`

**Overflow behavior**: Concrete-width integer arithmetic uses two's complement wrapping; exact compile-time integer arithmetic remains exact. Float behavior follows IEEE 754 standard.

---

## Declarations

**Global variables**:
- **Immutable (constants)**: `name[:Type] = expr`
  - Initialized with compile-time constant expression
  - Cannot be modified at runtime
  - Usable in compile-time expressions
  - Backend can place in ROM or optimize away entirely
  - Pure functions can compute initial values at compile time
  - **Resolved on demand from dependencies**
  - Can reference immutable globals declared anywhere in the loaded program
- **Exported immutable global**: `^name[:Type] = expr`
  - ABI-visible symbol
  - Must be immutable and compile-time constant
  - Type must be ABI-safe data (`primitive`, fixed-size arrays of ABI-safe data, or named structs recursively composed of ABI-safe fields)
  - Tuple types, type variables, and unresolved type expressions are rejected at the ABI boundary
  - Exported globals are retained as ABI roots (not removed by dead-code elimination)
- **Mutable (variables)**: `name:Type` or `name:Type = expr` when `expr` is not compile-time constant
  - Initialization at declaration is allowed
  - Can be modified at runtime
  - Declarations without initializer have undefined contents until first assignment
  - A non-constexpr initializer executes once, before the storage's first
    runtime read or write. An unused declaration executes no initializer.
    Initializer reads establish initialization dependencies. Reentering an
    initializer while it is running is an error or runtime trap.
  - Assignment evaluates its right-hand side before the persistent write;
    initialization required by that write occurs after the right-hand side.
    Reading uninitialized storage is an error or runtime trap. A whole-value
    assignment establishes initialization; updating a field first requires
    an initialized containing value.
  - Persistent reads and writes are observable runtime effects. Their values
    do not become compile-time constants merely because an earlier source
    statement stores a known value. A type query does not perform an access.
  - Backend typically places in RAM
  - Must be initialized before use
- **External symbol global**: `!name:Type;`
  - Declares a symbol provided outside the compilation unit
  - No initializer allowed
  - Treated as runtime-readable/runtime-writable by frontend analysis
  - Top-level only
- **Backend-bound global**: `!!name:Type;`
  - Declares a backend-resolved binding (address/segment/channel details come from backend-specific annotations)
  - No initializer allowed
  - Treated as runtime-readable/runtime-writable by frontend analysis
  - Allowed at top level and in local scopes
  - Language rules validate the logical binding; the target contributes its
    access and storage obligations during resolution. The MSX realization
    supports explicit `[[msx_address(N)]]` memory bindings and `[[msx_port(N)]]`
    byte I/O bindings. Other targets may bind a different address space or
    access protocol without changing source value semantics.

**Declaration order**:
- No forward declarations needed (whole-program compilation)
- Functions can be defined anywhere and called from anywhere
- Type definitions can appear anywhere
- Global constants resolve in dependency order, independent of source order
- Demanded constant cycles that do not establish a value are resolution errors
- At runtime, all symbols are accessible regardless of definition order

**Inline internal functions**: `&name(p[:Type],...)[-> Type] block`
- Returns last expression or `-> expr;` for explicit return
- Type inference when `->` omitted
- Omitted return types are inferred through the result's dependencies; each consuming operation requires only the properties it needs, subject to final emission and ABI requirements
- `->` is the only return operator (last expression = implicit return, `-> expr;` = explicit, `->;` = empty)
- Return exits only the current function (not enclosing functions)
- May be eliminated if unused
- **Inline-by-default lowering**: by default, internal functions are *inlined* by the frontend into their call sites and are not required to exist as standalone emitted functions.
- Use `&~` when an internal function must remain materialized (for recursion, code size control, or backend visibility).
- Recursive internal functions (including mutual recursion cycles) must be declared `&~`; otherwise inlining would not terminate.
  - Runtime recursive calls have a closed invocation interface: parameter and
    result types, captured locations, and conservative effects. A recursive
    return type may be inferred from base paths and surrounding type relations;
    an unanchored type cycle requires an explicit return annotation or another
    grounded interface relationship. The solver ties recursive calls to shared
    result promises; accepting a residual call never requires proving that its
    recursive value computation terminates.
  - Recursive state inference has a conservative closure rule. Ordinary body
    relationships may infer capture modes. If they do not close, retain the
    lexically reachable state and check that single interface. Do not enumerate
    subsets or modes of captures until some hypothetical interface validates.
    Every required type and incoming value must be supplied, and every outgoing
    value must be definitely initialized. If that interface cannot close, move
    the state into explicit typed parameters/results, or initialize/annotate the
    required locations. This can demand state mentioned by an unselected overload
    in a recursive group; explicit state avoids that conservative dependency.
    Nonrecursive automatic capture inference remains available.

    ```vexel
    &~count(k:#u8, state:#u16)->#u16 {
        k==0 ? state : count(k-1, state+1)
    }
    &^entry(n:#u8) { count(n, 11) }
    ```

    Known-input recursion still executes normally. The conservative runtime
    interface does not invent captured values or constrain compile-time recursion
    to a finite solver domain.
  - Known arguments may specialize a recursive invocation. If a recursive edge
    changes such an argument, the solver may generalize it into a runtime
    parameter and revise all derived facts. Invariant arguments can remain
    constants, including when they determine fixed shapes.
  - Mutable captures are logical locations shared across activations. Recursive
    storage and its post-call facts cannot use the first activation's values as
    invariants. Physical frames must support overlapping activations.
    Constant specialization must account for the captured state it actually
    reads: identical explicit arguments can still make progress when that state
    changes. A call returns updated captured locations without exposing its
    automatic local locations to its caller. Earlier reads keep their own
    reaching values.
  - A type query may close a recursive signature equation without closing a
    value equation. The signature projection must remain distinct from an
    execution instance: later demanding constant execution must still resolve
    its actual argument values and control paths. Pure values that select
    different return types remain legitimate type dependencies.

    Closing a recursive type or arithmetic equation is not proof that a value
    invocation finishes. For example, `&~again()->#u8 {again()}` has a declared
    result type, but evaluating `again()*0` cannot be replaced by a completed
    zero. The demanded call still needs completion or a residual execution that
    preserves its control behavior. Re-entering an unfinished constant invocation
    does not supply a base case; bounded search may report unresolved or exhausted.
    Discarding a call's result does not discard its completion or state changes.
    A later expression whose type depends on those changes observes the returned
    state; a declared return-type query remains independent of executing the call.
- Can be nested within other functions or scopes
- Nested functions automatically capture enclosing scope variables by reference
- A capture is a dependency on a logical location. Its type, incoming value and
  outgoing writes are distinct facts. Reading only a type does not initialize
  that location or require its value. A whole-location overwrite can establish
  an output without reading an old value; a partial update retains the untouched
  parts and therefore needs their values. A backend may omit unused incoming
  or outgoing storage only after proving these requirements for the selected
  body and its transitive calls. The type is the location's reaching type at the
  invocation, including dimensions established by earlier state changes.
- Can access all globals (both mutable and immutable)
- Captured variables remain accessible throughout nested function's lifetime
- Nested functions cannot outlive their enclosing function (no function types)
- Nested functions can be recursive

**Non-inlined internal functions**: `&~name(p[:Type],...)[-> Type] block`
- Behaves like an internal function, but is an explicit lowering barrier: the frontend does not inline it by default.
- Still internal (not ABI-visible) and may be eliminated if unused.

**Expression parameters**: Parameters prefixed with `$` capture unevaluated expressions
- `&name($p[:Type],...)` - `$p` is substituted as expression at each use site
- Enables macro-like behavior without separate preprocessor
- Example: `&repeat2($a) { $a; $a }` called as `repeat2(print("hello"))` evaluates twice
- **Hygiene**: Variables in `$p` resolve in caller's scope, not function's scope
- **No infinite expansion**: Each `$p` evaluated fresh (can reference variables modified by previous evaluation)
- Type-checked at instantiation site
- Can mix expression and value parameters: `&func(x, $y)`

**Reference-taking functions (methods)**: `&(receiver)#TypeName::methodname(p[:Type],...)[-> Type] block`
- Single receiver binds to the call-site receiver expression (which may be any expression)
- A mutable lvalue receiver denotes a bound logical location for the duration of the call. Its designation is evaluated once before ordinary arguments.
- A receiver that is not a mutable lvalue is copied into a private temporary.
- Reads and writes through overlapping receiver locations observe the same ordered state. This also specifies a read-only method whose argument expressions update its receiver.
- A backend may realize a receiver by value when it proves that doing so preserves these observations; pass-by-value is an optimization, not a second source alias rule.
- Receiver identifier is an lvalue; type annotation is optional and defaults to `TypeName`
- Parameters (non-receiver) passed by value
- `TypeName::` creates a method namespace (separate from general functions)
- Called via dot notation: `receiver.methodname(args)`
- Example: `&(v)Vec::push(item) { ... }` called as `v.push(42)`
- Methods and functions have separate namespaces: `&Vec::push` and `&push` don't conflict

**Operator methods**: `&(lhs)#TypeName::+(rhs[:Type])[-> Type] block`
- Overloads a core operator for instances of `TypeName`; the operator name may be a scalar operator (e.g. `+`, `==`) or a dotted per-element operator (e.g. `.+`, `.==`, `.&&`, `.<<`)
- Operator methods behave like regular methods: the left operand is the receiver and may be any expression; mutating operators may materialize a temporary for non-lvalues
- When the left operand has type `TypeName`, the compiler resolves `lhs <op> rhs` as a call to `TypeName::<op>` before attempting built-in operator rules
- Operator methods may not declare expression parameters and must accept exactly one receiver; value parameters model the remaining operands (binary operators expect one value parameter)
- Return types follow normal rules: arithmetic operators typically return `TypeName`, comparison operators return `#b`
- Scalar logical operator methods (`&&`, `||`) and dotted logical operator
  methods (`.&&`, `.||`) have ordinary value parameters: both operands evaluate
  before the call. Only the built-in scalar Boolean operators short-circuit.
  An unrelated receiver overload does not demand a skipped right operand.
- Dotted operators do not imply built-in broadcasting semantics by themselves; broadcasting is a separate higher-level semantics feature
- Absent an overload, built-in semantics apply (primitives retain their native arithmetic and comparison rules)

**Iteration methods**: `&(receiver)#TypeName::@($loop) block` and `&(receiver)#TypeName::@@($loop) block`
- Provide custom iteration semantics for `receiver@{...}` and `receiver@@{...}`. The method name must be `@` or `@@`, with exactly one receiver and exactly one expression parameter (the loop body).
- The compiler expands the method inline (functions with expression parameters are never emitted); the method must bind `_` before evaluating `$loop` for each element. Typical pattern: declare `_` locally, assign the current element, then evaluate `$loop`.
- `@@` methods are responsible for delivering elements in sorted order. `@@` is compile-time only: if sorted iteration cannot be decided at compile time at the call site, compilation fails.
- The iterable expression may be any expression; if the iteration method mutates the receiver, the compiler may materialize a temporary.
- The supplied body is statement-valued. `->|;` leaves the entire custom
  iteration. `->>;` finishes that evaluation of the supplied body and resumes
  the iteration method immediately afterward, including its advancement code.
  These targets belong to the syntactic custom iteration, independently of
  loops used inside the method's implementation. A return in the supplied body
  still returns from its caller's function. Forwarding the body through other
  expression parameters preserves these targets.

**Reference-taking functions (multi-receiver)**: `&(receiver1, receiver2, ...)methodname(p[:Type],...)[-> Type] block`
- Receivers evaluate left-to-right
- Each receiver is a bound logical location or a private temporary, as for a single receiver
- Each receiver must be an identifier; type annotations are optional and inferred from use
- **Cannot use TypeName:: syntax** (ambiguous which receiver's type)
- Called via tuple notation: `(r1, r2).name(args)`
- Example: `&(v1, v2)merge(threshold) { ... }` called as `(v1, v2).merge(10)`

- Receiver aliases cannot escape: they may not be stored in globals, returned, or captured beyond the call frame

**Tuple return types**: `-> (Type1, Type2, ...)` for multiple return values
- Syntactic sugar for anonymous struct types
- Example: `&divmod(a:#i32, b:#i32) -> (#i32, #i32) { (a / b, a % b) }`
- Tuple construction: `(expr1, expr2, ...)` creates anonymous struct
- Multi-assignment: `a, b = divmod(10, 3)` desugars to temp struct + field access
- Compiler generates anonymous types: `#__Tuple2_i32_i32(__0:#i32, __1:#i32)`
- These synthetic names are compiler-internal; user code references them only through tuple literals or multi-assignment
- Can be compile-time evaluated

**Exported functions**: `&^name(p:Type,...)[-> Type] block`
- Callable externally
- Return type must be concrete at the ABI boundary
- ABI boundary types only:
  - Primitives (`#iN`, `#uN`, `#f16`, `#f32`, `#f64`, `#b`, `#s`)
  - Named structs whose fields recursively use primitives, fixed-size arrays, or named structs
  - Top-level arrays are not allowed in parameters/returns (wrap in a named struct instead)
  - Tuple returns are not allowed
- Backends may restrict the accepted integer widths at ABI boundaries
- Cannot be eliminated
- Serve as entry points for executables or library exports

**External functions**: `&!name(p:Type,...)[-> Type];`
- Declaration only
- ABI boundary types only:
  - Primitives (`#iN`, `#uN`, `#f16`, `#f32`, `#f64`, `#b`, `#s`)
  - Named structs whose fields recursively use primitives, fixed-size arrays, or named structs
  - Top-level arrays are not allowed in parameters/returns (wrap in a named struct instead)
  - Tuple returns are not allowed
- Backends may restrict the accepted integer widths at ABI boundaries
- Backend defines calling/linking
- ABI-visible function overloads are not supported; an external function name must be unique within its scope/module

**Function overloading**:
- Internal functions and methods may share a surface name as an overload set
- Overload resolution uses:
  - receiver count
  - argument count
  - concrete receiver types
  - concrete argument types
- Generic and non-generic candidates participate in the same overload set
- Candidate discovery order is irrelevant. A winner must be justified against the complete applicable declaration set and all relevant argument constraints; a provisional winner may be revisited.
- If exactly one candidate matches, that candidate is selected
- If more than one candidate matches with the same best specificity, compilation fails with an ambiguity error
- Function overload sets share the same surface namespace as values/constants
- Types live in a separate surface namespace, so a type name may coexist with a function overload set of the same name

Applicability and specificity precede validating the selected body. A body
failure is a failure of the selected call, not evidence that a less specific
overload should run. Equal best candidates remain ambiguous even if their
bodies return the same value. For example, with `&f(x:#u8){1}` and
`&f(x:#u16){1}`, the call `f(1)` is ambiguous under the current numeric matching
rules; `f((#u8)1)` selects the byte overload. An annotation on the call's result
cannot break the tie. These are semantic policies, independent of candidate
exploration order or physical optimization cost.

**Type constructors**: `#Point(x:#i32, y:#i32);`
- Can be defined at module level or within any scope
- Scope determines visibility

**Import**: `::path::to::module;`
- No aliases (simplified from v1.6)
- Can import within any scope
- Multiple import statements are allowed; they must agree with duplicate-definition rules below
- No shadowing allowed (compilation error)

---

## Expressions & Control

- **Function call**: `fname(args)` (includes external functions)
  - Receiver expressions (if any) evaluate left-to-right before the argument list
  - Arguments then evaluate left-to-right; call dispatch happens after all operands finish
  - Qualified spelling `a::b::fname(args)` is accepted; frontend resolution currently binds by the final segment (`fname`) after import flattening
- **Method call**: `receiver.fname(args)` (bound logical receiver location; private temporary for non-lvalues)
  - An annotated receiver may constrain an existing location, but cannot change
    that location's type through an implicit conversion. A non-lvalue receiver
    may convert into a fresh private location before the method runs. Explicitly
    cast a value to request such a copy from an existing location.
- **Existence probe**: `receiver.?member` / `receiver.?fname(args)` / `(a, b).?fname(args)`
  - Compile-time only, non-evaluating, and non-side-effecting
  - Uses the same structural applicability and specificity rules as ordinary access/calls, including receiver mode and full call shape
  - Tests the available signature, without validating the selected body or evaluating actual arguments. An exact integer expression matches a numeric parameter structurally; representability and arithmetic validity are obligations of an executed call. Thus `p.?add(1/0)` may be true even though `p.add(1/0)` cannot execute.
  - A record's nominal identity and declared field names suffice for a member probe; unrelated field values and schemas are not demanded. A signature with unannotated parameters need not demand their complete types.
  - Returns `#b`
  - Function capability: `?fname(args)` uses these same rules with no receivers. It is distinct from a finite requirement `?(predicate)` and a solve region `?{ ... }`.
  - Missing functions/fields/methods and closed nonmatching signatures yield `0`
  - Ambiguous probes or probes that depend on unresolved receiver/argument types are compile-time errors
- **Constructor**: `#TypeName(args)`
  - In statement position, bare `#TypeName(...)` parses as a type declaration; wrap constructor statements in parentheses (`(#TypeName(...));`) when needed
- **Member**: `x.y`
- **Index**: `arr[expr]`
- **Block**: `{ stmt; ... }` evaluates statements, yields last expression
- **Capability-guarded execution**: `?write(bytes) ? { write(bytes); };` or `device.?write(bytes) ? { device.write(bytes); };`
  - The probe closes only signature availability. It does not speculate on whether a whole body will validate or terminate.
  - A selected body has ordinary typing, initialization, constraint, execution and completion obligations. Errors cannot discard its stores or choose a less-specific overload.
  - A false probe skips its branch by the ordinary conditional rule. Unresolved, ambiguous or exhausted probes are never converted to false.
  - `#{ ... }` is rejected with a migration diagnostic, including in an unused body. There is no implicit semantic rollback or legacy compatibility mode.
  - Imports are unconditional declarations in their lexical scope. An optional platform service must be exposed through an available interface (for example a project-local `std` module), not discovered by catching an import failure.
- **Conditional (expression)**: `cond ? expr_true : expr_false`
  - Compiler attempts to evaluate cond at compile time
  - If successful (compile-time constant), branches can have different types (dead branch eliminated)
  - If not (runtime value), branches must have same type
  - Everything is compile-time unless it cannot be
  - Nested conditionals without parentheses are a compile error
  - Example: `a ? b : c ? d : e` is an error (must use parentheses)
- **Conditional (statement)**: `cond ? stmt` (no else branch, not an expression, has no type or value)
- **Iteration (unrolled)**: `iterable@expr` binds `_` to each element (unrolled loop)
  - `iterable` evaluates once before the loop starts; iteration walks the resulting array value (arrays and ranges only)
  - A transfer while evaluating `iterable` keeps its existing lexical target.
    The new loop's break/continue target is in scope only for its body. If
    iterable evaluation exits, neither an array value nor the body is demanded.
  - Unrolled by default: the frontend expands the loop body once per element. If the iteration count cannot be determined, compilation fails (use `!@` for a runtime loop).
- **Sorted iteration (unrolled, compile-time only)**: `iterable@@expr` binds `_` to each element of a sorted copy
  - `@@` copies the evaluated array, sorts it at compile time using the comparison rules in §2, then iterates over the sorted copy (the original array is never mutated)
  - Sorting is stable for equal keys. Applicable `<` methods participate through
    ordinary overload resolution; unrelated methods leave builtin comparison
    available. A comparison must yield a compile-time Boolean without runtime
    effects or changes to enclosing storage. Receiver mutations affect a private
    comparison copy. The comparison must define a consistent ordering.
  - Always unrolled and compile-time only: if sorting cannot be decided at compile time, compilation fails (use an explicit sort function + `!@` loop for runtime sorting).
  - `_` is scoped to the loop body, read-only, and only available in iterable loops
  - `0..10@print(_)` prints 0 through 9; `arr@process(arr[_])` visits each array element
  - `arr@@print(_)` visits the same elements but in lexicographic order
  - Named types can override iteration by defining `&(self)#Type::@($loop)` / `&(self)#Type::@@($loop)` (see Iteration methods above); without an override, only arrays and ranges are accepted
  - Nested loops shadow `_` (innermost wins)
- **Process import**: `::"command" -> name;` executes host command `command` at compile time and binds stdout to immutable `name:#s`; implementations should default to disabling this behavior and provide an explicit opt-in flag (e.g., `--allow-processes`). Inputs enabling process execution must be treated as fully trusted.
- **Repeat (unrolled)**: `(cond)@expr` repeats expr while cond is true
  - Unrolled by default: `cond` must be decidable at compile time for each iteration; otherwise compilation fails (use `!@` for a runtime loop).
  - Condition re-evaluates before every iteration; no `_` is bound in this form
- **Runtime loop**: `iterable!@expr` / `(cond)!@expr`
  - Lowering barrier: the frontend must not unroll this loop, even if bounds are compile-time known.
  - Use when the loop trip count is runtime-unknown or when unrolling would be undesirable.
- **Loop control**: `->|;` (break), `->>;` (continue) - affect innermost enclosing loop
  - Tokens are contiguous (`->|`, `->>`); spaced forms (`-> |`, `-> >`) are not valid
  - A repeat condition is inside its loop's control scope. A break in the
    condition exits the loop; a continue starts the next condition evaluation,
    preserving preceding updates and skipping the current body. A return leaves
    its lexical function. A condition that transfers need not produce a Boolean.
- **Return**: `-> expr;` returns value from current function; `->;` returns without value
- **Length/Absolute**: `|expr|` returns (type-dependent):
  - Arrays: compile-time constant length (size of array)
  - Strings: compile-time constant length (number of bytes)
  - Signed integers/floats: absolute value (runtime calculation)
  - Unsigned integers: identity (`|x| = x`)
  - Example: `0..|arr|@process(arr[_])` iterates entire array
- **Range**: `a..b` is syntactic sugar for array literal
  - `0..5` is equivalent to `[0,1,2,3,4]` (upper bound excluded)
  - `5..0` is equivalent to `[5,4,3,2,1]` (descending)
  - `a..a` where a == b → compile error (would produce empty array)
  - Both bounds must be compile-time constants
  - Type inference follows array literal rules (collective type for all elements)

**Precedence** (high to low):
- Postfix (call, index, member)
- Unary (-, !, ~, | for length/absolute)
- Multiplicative (* / %)
- Additive (+ -)
- Range (..)
- Shifts (<< >>)
- Comparison (`==`, `!=`, `<`, `<=`, `>`, `>=`) — non-associative (chained comparisons are rejected without parentheses)
- Bitwise AND (&)
- Bitwise XOR (^)
- Bitwise OR (|)
- Logical AND (&&)
- Logical OR (||)
- Conditional (?:)
- Assignment (`=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`, `&&=`, `||=`)
- `@` / `@@` / `!@` (iteration/repeat) bind looser than assignment; parentheses required to combine with other operators

**Assignment**:
- Single assignment: `x = expr` returns the assigned value
- Compound assignment: `x op= y` is assignment with the corresponding operator (`x = x op y`) and returns the assigned value
  - Supported families: arithmetic, bitwise, shifts, logical (`&&=`, `||=`)
  - Built-in `&&=` and `||=` preserve logical short-circuit semantics. An
    applicable operator method follows ordinary eager method-call semantics.
  - The left-hand lvalue is evaluated once
- Dotted per-element compound assignment: `x .op= y` lowers as `x = x .op y` and returns the assigned value
  - Supported dotted families mirror the scalar compound-assignment families (`.+=`, `.-=`, `.*=`, `.||=`, ...), subject to the availability of the corresponding dotted operator semantics/overload
  - Dotted logical compounds (`.&&=`, `.||=`) are **non-short-circuit** (element-wise semantics)
  - The left-hand location is designated once before evaluating the right-hand
    operand, including any index calculation with effects.
- Can be used in larger expressions: `y = (x = 5) + 1` assigns 5 to x, 6 to y
- Evaluation is right-to-left for chained assignments: `a = b = c` evaluates as `a = (b = c)`
- **Multi-assignment**: `a, b, c = expr` where `expr` returns tuple type
  - Syntactic sugar: desugars to temp variable + individual assignments
  - Example: `q, r = divmod(10, 3)` → `__tmp = divmod(10, 3); q = __tmp.__0; r = __tmp.__1`
  - Number of variables must match tuple arity
  - This is a local statement with no value. The right-hand tuple is evaluated
    once before any destination is changed; destinations are assigned left to
    right using ordinary assignment conversions and receiver-place semantics.

**Parsing rules**:
- && binds tighter than ||
- Comparison operators bind tighter than bitwise operators
- Dotted per-element operators mirror the precedence/associativity of their scalar counterparts (for example `.*` binds like `*`, `.+` binds like `+`)
- Dotted per-element operators over arrays use strict broadcasting (trailing-dimension alignment, singleton expansion, scalar lifting)
  - Incompatible shapes are compile-time errors (no implicit transpose/reshape/flatten)
  - Operands evaluate once before element calls. An explicit dotted operator
    overload takes precedence; otherwise the scalar operator is applied to
    each element, including applicable scalar operator methods.
  - Lifted receivers are private element copies. Calls execute in array order,
    preserving effects and captured writes. A dotted compound assignment stores
    the resulting array after those calls. Empty shapes resolve the element
    result type without executing a sample call.
- Ambiguous expressions require parentheses
- Nested conditionals must be explicitly parenthesized
- In statement position, a leading `#Name(...)` is parsed as a type declaration; constructor-only statements should be wrapped as expressions (for example `(#Name(...));`)
- Parser errors on ambiguity rather than guessing intent

## Annotations & Lowered Form

- Annotations are metadata prefixes: `[[name]]` or `[[name(arg1, arg2)]]`. Multiple may be stacked (e.g., `[[backend_hint]] [[schedule(io)]] &^foo(...)`).
- Placement: before functions/methods, type declarations, globals, statements, parameters/fields, and expressions.
- Unknown annotations are preserved verbatim; they do not change language semantics.
- Arguments have ordinary expression syntax. A recognized relationship demands
  the properties it needs; inert tooling metadata does not execute its arguments.
- The current target vocabulary distinguishes supplied `[[fact(key, value)]]`,
  hard `[[require(key, value)]]`, and advisory `[[prefer(key, value)]]`
  relationships. These attach to module declarations, including declarations
  imported from project descriptions. The key is a name/string and the value
  is ordinary source data. CLI contributions enter the same equations. Conflicting
  explicit requirements are contradictory; source order and origin grant no
  priority. Defaults apply only in the absence of contributions. Unknown keys
  within these recognized relationships are errors, not ignored requirements.
- `[[source]]` marks a requested source root. `[[stream(capacity)]]` requests
  repeated, serialized execution with a bounded pool of independently owned
  results. Capacity is a source expression. Each successful submission creates
  a lease valid until release; backpressure performs no source execution.
  The target must reject stale leases and prevent token wrap from reassigning
  their identity. This adds no heap or general source pointer.
- `[[no_feedback]]`, `[[reads(resource)]]` and `[[writes(resource)]]` attach
  boundary facts to external functions. An adapter must fulfill them. They
  do not make an external observation pure. Arbitrary metadata cannot authorize
  concurrent writes or recursive static-frame sharing.
- `[[range(lower,upper)]]` supplies closed input bounds; `[[msx_address(expr)]]`
  and `[[msx_port(expr)]]` supply target-space bindings. These demand their value
  expressions through the resolver. `[[mapper]]` requests mapped placement for
  owned persistent storage; an immutable constant may still need no storage.
- See the [target contract](backend-msx-neo8.md) for typed keys, supported ranges,
  preference combination, runtime ownership APIs and stack assumptions. Work
  limits, diagnostic verbosity and output destinations remain operational
  settings. They may stop a solve; they cannot select a different source meaning.
- Resolved Vexel may be an inspectable projection of a region with an explicit interface and assumptions. It is not a mandatory phase barrier: target facts and physical constraints may participate before other regions resolve. The current prototype exports an editable C/assembly target project; it does not yet serialize resolved Vexel.

---

## Name Resolution & Modules

- Values/functions share one namespace per module (including overload sets); type names use a separate namespace
- Import: `::A::B;`
- **Closed declaration membership**: Each lexical scope has an immutable set of
  declarations and statically named imports for one input revision. Collect the
  full scope/import closure before dispatch or a negative capability result.
  A function body can introduce declarations into its own scope, never into a
  caller's already established overload set. Moving independent declarations
  or imports within a scope does not change membership or source meaning.
- This closes membership only. Argument types, applicability, selected bodies,
  values, shapes and backend realization can remain unknown and be refined.
  A source/import/resource edit starts a new input revision and invalidates
  dependent answers; it cannot amend an old revision's declaration set.
- Resource expressions: `::A::B::file.ext` used in expression context load the referenced resource at compile time
  - Files resolve using the same search order as modules (project root, then relative to the importing file; `std/*` uses the standard-library search rule below)
  - A file evaluates to a `#s` (immutable string) whose bytes exactly match the file contents (binary-safe)
  - A directory evaluates to an array of tuples `(__0:#s, __1:#s)` (one per regular file, lexicographic order, non-recursive); `.__0` is the file name and `.__1` the file contents. Empty or missing directories yield an empty tuple array.
  - Process imports: `::"command" -> name;` execute `command` at compile time (via the host shell) and bind its captured stdout as an immutable `#s` named `name`
  - Resource expressions are immutable constants and may appear in initializers or expressions; attempting to mutate them is a compile-time error
  - Example: `font:#s = ::assets::font.bin;` embeds `assets/font.bin` as a string, while `sprites = ::assets::tiles;` embeds the contents of each file within `assets/tiles/`
- Symbols are internal by default; export explicitly with `&^` (functions) or `^` (globals)
- Resolution: `::a::b;` maps to `a/b.vx` (case-sensitive, relative to project root)
- Standard library module path: `::std::...;`
  - `std` modules are resolved per module path:
    1. project-local `std/<module>.vx` (override)
    2. bundled compiler `std/<module>.vx` (fallback)
  - No symbol-level merging; overrides replace the whole module path
  - `::std::*` does not search relative to the importing file
  - Bundled `std/math.vx` is compiler-recognized only when the bundled fallback module is selected
    - project-local `std/math.vx` overrides are ordinary modules and receive no builtin folding/runtime mapping
    - bundled unary and binary math calls may be folded at compile time when arguments are constexpr
    - bundled unary math calls lift over array-shaped values element-wise
    - bundled binary math calls lift over array-shaped values using strict broadcasting (trailing-dimension alignment, singleton expansion, scalar lifting)
    - arguments evaluate once in source order, including arguments to an empty
      result; lifting reuses their values and does not repeat their effects
    - result types and shapes can resolve without reading argument values
    - the scalar interface is:

      | Functions | Arguments and result |
      |---|---|
      | `abs(x)` | Any numeric type, preserved; concrete signed minimum wraps |
      | `min(a,b)`, `max(a,b)` | Common numeric type, using ordinary implicit conversions |
      | `sqrt(x)` | Floating point, preserved; correctly rounded nearest/even |
      | `floor(x)`, `ceil(x)`, `trunc(x)` | Numeric type, preserved; round toward minus infinity, plus infinity, or zero |
      | `round(x)`, `roundeven(x)` | Numeric type, preserved; nearest with ties away from zero or to even |
      | `copysign(a,b)` | Common floating type; copy sign, retaining magnitude bits and NaN payload |
      | `isnan(x)`, `isinf(x)`, `isfinite(x)`, `signbit(x)` | Floating argument; Boolean result |
      | `isqrt(x)` | Integer type, preserved; floor of the nonnegative square root; negative inputs trap |
      | `fmod(a,b)` | Common numeric type; the language's signed remainder operation |
      | `pow(base,exponent)` | Numeric base type, preserved; exponent implicitly convertible to `#u32` |

    - Integer rounding is identity. Fixed-point rounding preserves the declared
      scale and wraps on overflow. Floating rounding preserves signed zero;
      NaN arithmetic results use the canonical quiet NaN. A negative floating
      square root is NaN, while `sqrt(-0)` is negative zero.
    - Floating `min`/`max` return the numeric argument if only one is NaN,
      canonical NaN if both are NaN, and prefer negative/positive zero respectively.
    - `pow` uses repeated squaring with the language's multiplication and rounding
      after each multiplication. It starts from an explicit conversion of `1`
      into the base type; exponent zero therefore returns that value, including
      for zero or NaN bases. General real exponents and transcendental functions
      are not part of this initial bundled interface.
    - These operations also contribute finite relations. A candidate-specific
      numeric trap excludes that assignment; exhausting a solver budget never
      counts as proof that an assignment is impossible.
  - Bundled `std/bits.vx` is compiler-recognized only when the bundled fallback module is selected
    - project-local `std/bits.vx` overrides are ordinary modules and receive no builtin folding/runtime mapping
    - bundled bit reinterpretation helpers (`f32_as_u32`, `u32_as_f32`, `f64_as_u64`, `u64_as_f64`) may be folded at compile time when arguments are constexpr
    - The solver implements these four helpers in ordinary Vexel using canonical
      byte representation casts. This gives constant and residual uses identical
      semantics without requiring privileged recognition of function names.
- Scoped imports instantiate their module once per lexical scope; instances do not share mutable state
  - Repeated runtime calls through the same importing lexical scope share its
    persistent storage. Different specializations do not create fresh module
    storage. Separate importing lexical scopes have separate storage.
  - Imported module declarations may refer to visible declarations but do not
    capture automatic variables or expression parameters from an importing
    activation. Nested functions written directly in a block retain ordinary
    lexical capture semantics.
  - A named immutable global may be a read-only receiver. A method that attempts
    to store through that receiver is rejected. Copy into a local variable to
    obtain an independently mutable value.
- Re-importing the same module in the same scope is permitted only when every top-level definition is identical (functions/types textually equal; constants equal after compile-time evaluation); otherwise it is a compile error
- Imports can be scoped to blocks
- Type definitions can be scoped to blocks (visibility limited to that scope)
- Function definitions can be scoped to blocks (nested functions)
- Nested functions capture enclosing scope variables by reference
- No import aliases
- No shadowing (compilation error if names conflict), except:
  - `_` may shadow in nested iterations
  - functions may form overload sets with other functions of the same surface name
  - type names live in a separate surface namespace from functions
- **Resolution order**: Imports establish the available declarations. Immutable constants resolve from dependencies; demanded nonproductive value cycles are errors.
- **Forward references**: Functions, types, and immutable constants can be used before definition.

---

## Type Inference & Generics

- Invocation-specific type constraints; full Hindley-Milner inference remains a proposal beyond the executable solver profile
- Omitted parameter/field types create fresh type variables constrained by call sites and the operations using them; those requirements use ordinary expression syntax
- Unresolved integer literals may flow through inference until a concrete requirement appears
- Implementations may offer strictness modes that reject unresolved flow earlier; this strengthens diagnostics, not core runtime semantics
- Return type properties may remain unresolved until demanded; a known field or shape can be used without prematurely fixing unrelated properties
- Argument types include full array shape (element type + length); mismatched lengths form distinct instantiations
- Generic functions may not be exported or external; they must resolve within the compiling program
- Specialization semantics are pure language behavior—implementations may inline, share, or eliminate copies as long as observable semantics match a concrete instantiation
- Conditional: `a ? b : c` requires `type(b) == type(c)` only if `a` is runtime, and `a` must be `#b`
- Compile-time conditionals allow different types (dead branch eliminated)
- Array sizes: `#i32[10]` != `#i32[20]`
- Nested functions capture enclosing scope by reference

---

## Runtime Semantics

**Evaluation**:
- Strict left-to-right for all expressions
- `&&` and `||` short-circuit and require boolean operands
- `@` (iteration and repeat) does not return values
- `!` as logical NOT returns boolean
- Statement conditionals (`cond ? stmt`) do not produce values and require boolean conditions

**Compile-time execution**:
- Array sizes and immutable global initializers must be compile-time constants
- Immutable globals (initialized at declaration) are compile-time constants
- Functions execute at compile time when needed for constants (no explicit purity annotation)
- **Constant resolution is demand-driven**: Imports make declarations available; immutable globals resolve from dependencies
- **During constant resolution**: Immutable constants may be referenced regardless of declaration order
- **Compile-time execution constraints**:
  - Cannot call external functions (`&!`), except compiler-recognized bundled `std/math` and `std/bits` intrinsics when the bundled modules are in use
  - Cannot modify mutable globals
  - Can read immutable globals whose value dependencies resolve
  - All paths must be deterministic (no runtime-dependent branches)
  - Accessing uninitialized values triggers compile-time error
- **Purity determination**: Compiler attempts compile-time execution and fails if constraints violated
- Compile-time arithmetic follows language semantics independently of the host:
  - Can use any type (even unsupported by target)
  - Results must be convertible to target types
  - Example: use #f64 math to build #u8 lookup table
  - A computation may depend on explicit target facts such as a bank capacity. Unknown runtime capabilities must remain runtime inputs; neither host properties nor guessed placement choices become source constants.
- Compile-time conditionals eliminate dead branches before type checking
- Pure compile-time function evaluation performs no external calls, I/O, or side effects (`process import` is a separate opt-in module feature)

**Memory**:
- Primitives by value
- Composites: value semantics (assignment copies)
- Assignment always creates independent copy (no aliasing)
- Backend may optimize to copy-on-write or move when safe
- No heap
- Logical lifetimes; physical storage chosen under target and reentrancy constraints
- Automatic mutable state belongs to an activation; persistent module storage
  has the lifetime of its lexical module instance
- Automatic lifetime: end of scope; persistent lifetime: the runtime instance

### Operations
- Arrays: `arr[i]` indexing
- Strings: indexing yields `#u8` (read-only, cannot modify string contents)
- Equality: value-based (deep comparison)
- Assignment: always copies (no aliasing)
  - `b = a` creates independent copy of a
  - Modifying b doesn't affect a
  - Backend optimizes unnecessary copies
- Division by zero: runtime trap
- Remainder operator (`%`): integer operands; signed remainder follows division truncated towards zero
- Bitwise operators (`&`, `|`, `^`, `~`, `<<`, `>>`): integer operands; signed right shift is arithmetic
- Length/absolute operator `|expr|`:
  - arrays: compile-time length (array size)
  - strings: runtime or compile-time length
  - signed integers/floats/fixed-point: absolute value
  - unsigned integers/fixed-point: identity
  - other operand types are rejected
- Shift amounts must be non-negative and less than the typed operand's bit
  width. An invalid constant shift is an error; an invalid runtime shift traps.
  Shifting an unresolved exact integer is instead bounded by the resolver's
  integer construction budget.
- Boolean arrays can be converted to/from integers via casting (creates copy)
- Composite types can be converted to/from byte arrays via casting (creates copy)

**Execution**:
- Exported functions (`&^`) are entry points
- Exported globals (`^name = ...`) are ABI-visible data symbols
- Compiler mode determines execution:
  - Executable: compiler specifies entry point from exports
  - Library: all exports visible
  - Backend generation: exports become backend ABI symbols (functions and globals)

---

## Errors & Diagnostics

**Compile-time**:
- Syntax errors
- Name resolution failures
- Type mismatches
- Generic constraint violations
- Array size not compile-time constant
- **Literal overflow**: Integer or float literals that exceed type range
- **Ambiguous parsing**: Multiple valid parse trees trigger an error
  - Parser requires explicit parentheses to disambiguate
  - Example: nested conditionals, complex expressions
  - Error message indicates where parentheses needed

**Runtime**:
- Division by zero traps
- Integer overflow wraps (two's complement)

---

## Grammar

The grammar below is operational (close to parser behavior). `';'` is optional on expression/variable statements when the next token boundary is unambiguous.

```
program      ::= { top }

top          ::= [ annots ] top_item
top_item     ::= func | func_noinline | export | extern | type_decl | import | var_decl

annots       ::= annotation { annotation }
annotation   ::= '[[' ident [ '(' [ ann_arg { ',' ann_arg } ] ')' ] ']]'
ann_arg      ::= expr

func         ::= '&' [ recv_prefix ] func_name '(' [ params ] ')' [ '->' ret_spec ] block
func_noinline ::= '&~' [ recv_prefix ] func_name '(' [ params ] ')' [ '->' ret_spec ] block
export       ::= '&^' [ recv_prefix ] func_name '(' [ params ] ')' [ '->' ret_spec ] block
extern       ::= '&!' [ recv_prefix ] func_name '(' [ params ] ')' [ '->' ret_spec ] ';'

recv_prefix  ::= '(' refparams ')' [ '#' ident '::' ]
func_name    ::= ident | op_name
ret_spec     ::= type
op_name      ::= '+' | '-' | '*' | '/' | '%' | '!' | '~' | '&' | '|' | '^'
               | '<<' | '>>' | '&&' | '||' | '==' | '!=' | '<' | '<=' | '>' | '>=' | '@' | '@@'
               | '.+' | '.-' | '.*' | './' | '.%' | '.==' | '.!=' | '.<' | '.<=' | '.>' | '.>='
               | '.&' | '.|' | '.^' | '.<<' | '.>>' | '.&&' | '.||'

params       ::= param { ',' param }
param        ::= [ '$' ] ident [ ':' type ]
refparams    ::= ident [ ':' type ] { ',' ident [ ':' type ] }

var_decl     ::= [ '^' ] ident [ ':' type ] [ '=' expr ] [ ';' ]
               | '!' ident ':' type [ ';' ]     // top-level only
               | '!!' ident ':' type [ ';' ]    // top-level or local
type_decl    ::= '#' ident '(' [ fields ] ')' ';'
fields       ::= ident [ ':' type ] { ',' ident [ ':' type ] }

import       ::= '::' mod_qname ';'
               | '::' string '->' ident ';'    // process import

block        ::= '{' { stmt } [ expr ] '}'
stmt         ::= [ annots ] stmt_core
stmt_core    ::= var_decl
               | ident ',' ident { ',' ident } '=' expr [ ';' ]
               | expr [ ';' ]
               | '->' [ expr ] ';'
               | '->|' ';'
               | '->>' ';'
               | expr ('@' | '@@' | '!@') expr
               | '(' expr ')' ('@' | '!@') expr
               | expr '?' stmt
               | type_decl
               | import
               | func
               | func_noinline

expr         ::= assign
assign       ::= cond | (lvalue assign_op expr)
assign_op    ::= '=' | '+=' | '-=' | '*=' | '/=' | '%=' | '&=' | '|=' | '^=' | '<<=' | '>>=' | '&&=' | '||='
               | '.+=' | '.-=' | '.*=' | './=' | '.%=' | '.&=' | '.|=' | '.^=' | '.<<=' | '.>>=' | '.&&=' | '.||='
cond         ::= logic_or ( '?' expr ':' expr )?
logic_or     ::= logic_and { '||' logic_and }
logic_and    ::= bit_or { '&&' bit_or }
bit_or       ::= bit_xor { '|' bit_xor }
bit_xor      ::= bit_and { '^' bit_and }
bit_and      ::= compare { '&' compare }
compare      ::= shift [ ('=='|'!='|'<'|'<='|'>'|'>=') shift ]
shift        ::= range { ('<<'|'>>') range }
range        ::= sum [ '..' sum ]
sum          ::= prod { ('+'|'-') prod }
prod         ::= unary { ('*'|'/'|'%') unary }
unary        ::= postfix | ('+'|'-'|'!'|'~') unary | '|' expr '|' | '(' type ')' unary
postfix      ::= primary { call | index | member }
primary      ::= literal | var | resource | '(' expr ')' | tuple | block | array
               | constructor | capability | solve | choice | requirement
constructor  ::= '#' ident '(' [ arglist ] ')'
capability   ::= '?' ident '(' [ arglist ] ')'
solve        ::= '?' block
choice       ::= '?' '(' type ',' expr ')'
requirement  ::= '?' '(' expr ')'

var          ::= ident { '::' ident } | '$' ident
resource     ::= '::' res_qname
call         ::= '(' [ arglist ] ')'
index        ::= '[' expr ']'
member       ::= '.' ident | '.?' ident
arglist      ::= expr { ',' expr }
array        ::= '[' [ expr { ',' expr } ] ']'
tuple        ::= '(' expr ',' expr { ',' expr } ')'

mod_qname    ::= ident { '::' ident }
res_qname    ::= path_seg { '::' path_seg }
path_seg     ::= ident { '.' ident }
ident        ::= [A-Za-z_][A-Za-z0-9_]*

type         ::= '#' type_atom { '[' expr ']' }
               | '(' type { ',' type } ')'
type_atom    ::= ident [ '.' decimal_width ] | '[' expr ']'
decimal_width ::= [0-9]+

lvalue       ::= var | index | member

literal      ::= number | float | string | char
number       ::= [0-9]+ | '0' [xX] [0-9A-Fa-f]+
float        ::= [0-9]+ '.' [0-9]+ [ ('e'|'E') [ '+' | '-' ] [0-9]+ ]
string       ::= '"' ( escape | [^"\\] )* '"'
char         ::= "'" ( escape | [^'\\] ) "'"
escape       ::= '\\' ( [nrt\\'"] | 'x' hex hex | octal )
hex          ::= [0-9A-Fa-f]
octal        ::= [0-7] [0-7]? | [0-3] [0-7] [0-7]

line_comment ::= '//' .*
```

Each supported dotted binary operator uses the same precedence as its
undotted form (for example `.+`, `.<<`, and `.&&`). In `|expr|`, an ungrouped
`|` closes the length/absolute expression; group an inner bitwise-OR expression
when needed. A decimal suffix after a type name denotes the fractional width
of an `iN.M` or `uN.M` fixed-point type. The tuple-type spelling applies in
annotations and casts as well as return signatures. Metadata can decorate
fields, parameters, declarations, statements and expression prefixes; its
names and literal arguments are retained without expression evaluation.

**Calls**:
- `fname(...)`: function call (internal or external)
- `receiver.fname(...)`: method (receiver by reference if mutating, otherwise by value)
- `(r1, r2).fname(...)`: multi-receiver method
- `#TypeName(...)`: constructor
