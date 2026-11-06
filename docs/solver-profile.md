# Executable solver profile

This is the contract implemented by `build/vexel`, not the complete language
vision in the RFC. Examples below assume semicolons where statement boundaries
could be ambiguous.

## Declarations and roots

```vexel
count = 3;
^answer:#u32 = compute(count);
&compute(x) { x * 14 }
&~recursive(x) { x == 0 ? 0 : recursive(x - 1) }
&^entry(x:#u16) -> #u16 { compute(x) }
&!device_read() -> #u16;
::path::to::module;
```

Constant initializers are immutable equations. Globals and functions may be
used before their declarations. `name:Type;` introduces persistent uninitialized
storage; a typed initializer that remains runtime introduces persistent storage
with one-time initialization on first access. A known effect-free initializer
remains immutable. Reads and writes to persistent storage retain observable
runtime effects. Unused initializers are not evaluated. A demanded nonproductive
definition cycle reports an unresolved obligation.

`!name:Type;` declares external storage. `!!name:Type;` declares a backend
binding, also allowed in a block. MSX realizes annotated memory addresses and
byte I/O ports; its generated plan describes each physical binding. Internal
storage traps on uninitialized reads and reentrant initialization. Named
constant receivers permit reads and reject mutation. Imported module storage
is shared across activations and specializations of one lexical import, while
separate lexical imports have separate storage.

By default, exports are roots. Exported parameters become runtime inputs.
`--root name` selects an ordinary or exported global; it can be repeated.
`--call name --arg 5 --arg -2` selects a concrete invocation with exact decimal
integer arguments. These explicit selections replace default export demand.
`--root` accepts an exact integer result without requiring an ABI type.

There are no reserved words. Names share one value/function namespace.
Shadowing is rejected, including parameters shadowing globals. Internal
functions may overload a name; exported and external names are unique.
Repeated imports of the same canonical source path are loaded once.

## Values and types

A function result with no possible return transfer has an independent type
interface. Querying it need not resolve unrelated earlier statement interfaces,
including elements of a local array used only for its shape. Source analysis
includes returns carried by expression arguments and iteration bodies. Ordinary
execution still demands the complete body, and state needed for a dimension or
other type expression remains a dependency of that expression.

Implemented source types:

- `#iN`, `#uN`: signed and unsigned integers of positive width N;
- `#b`: Boolean values zero and one;
- `#s`: immutable constant byte strings;
- `#T[N]`: fixed-size arrays; dimensions are constant expressions.

`#u8[2][3]` means an outer array of length three, containing arrays of two bytes.
Arrays have value semantics. No pointers, references, allocation operators, or
language-level heap are introduced. The C++ solver naturally uses host memory
to represent its graph and arbitrary-precision calculations.

An integer literal and arithmetic containing only exact integers have the
internal type `#integer`. It is not a source type name or a runtime machine type.
Exact values remain exact until a use site asks for a concrete representation.

```vexel
big = 18446744073709551616 * 3;     // exact, no host-width truncation
^a:#u128 = big;                   // checked concretization
^b:#u8 = (#u8)255 + 1;            // 0: concrete arithmetic wraps
// ^c:#u8 = 255 + 1;              // error: exact 256 does not fit
^d:#u8 = (#u8)(255 + 1);          // 0: explicit cast wraps
```

An expected result type is not a backwards constraint on an expression's
operands. If arithmetic is intended to wrap, put the concrete type on an
operand or a function parameter. This is the profile's numeric policy, not a
requirement imposed by solver architecture or scheduling. Operand declarations
may occur later in the source; their complete facts are demanded before this
prototype publishes the arithmetic result. The broader RFC proposes explicit
partial knowledge, assumptions and revisable answers. The finite solve regions,
independent type properties and structural revisions described below implement
these mechanisms within explicit, bounded contexts.

There is no runtime representation for `#integer`. For a runtime choice between
different integers, make at least one arm concrete:
`flag ? (#u8)42 : 1`. Writing `flag ? 300 : 1` and converting the result to
`#u8` afterwards is rejected rather than bypassing representability checks.

Integer operands in the same signedness family widen to the larger width.
An exact partner is checked against that concrete type. Implicit narrowing,
signed/unsigned conversion, and integer-to-Boolean conversion are rejected.
Exact zero and one can become Boolean; explicit Boolean casts test nonzero.

Concrete integer arithmetic wraps in two's complement. Division truncates
towards zero; signed remainder follows that quotient. Bitwise operations work
on integer representations. Signed right shift is arithmetic. Shift counts
must be nonnegative and below the concrete operand width; exact shifts use the
solver's bit budget. A known zero divisor is a resolution error. A runtime
divisor leaves a residual operation whose eventual execution must check zero.

Integers, strings, characters, arrays, and constant resources are implemented.
String and character escapes include `\n`, `\r`, `\t`, `\\`, `\"`, `\'`,
`\xHH`, and one to three octal digits (at most 255). Source is ASCII; escaped
strings and resource bytes may contain any byte, including zero.

## Expressions and local state

Implemented operators are `+ - * / %`, comparisons, integer bitwise operators,
shifts, Boolean `! && ||`, unary `+ - ~`, casts, and `|expr|` for
length/absolute value. Arrays and strings support indexing and lexicographic
comparison; array comparisons can compare different lengths.

Precedence, highest to lowest:

1. Direct calls and indexing
2. Unary operators and casts
3. `* / %`
4. `+ -`
5. `..`
6. `<< >>`
7. Comparisons (non-associative)
8. `&`, then `^`, then `|`
9. `&&`, then `||`
10. `?:`
11. Assignment
12. Iteration in statement position

Nested conditionals need parentheses. Assignment expressions inside conditional
arms should be enclosed in a block or parentheses.

```vexel
&sum(items) {
    total:#u32 = 0;
    items@{ total += _; };
    total
}
```

Typed local declarations use `x:#T` or `x:#T = value`. The first assignment to a
new local name, `x = value`, infers its type from that initializer. Later
assignments retain the slot's type. This is use-site inference, not full
Hindley–Milner inference or backwards type unification. In particular, a local
initialized with an exact literal remains exact; annotate it if it will hold
runtime machine values.

Local assignment returns the assigned value. Arithmetic and bitwise compound
assignments are supported on local names. Reads require an initializing store
on every activated path. Parameters are local value copies and can be
reassigned. Initialized local arrays support element assignment, nested indexing,
and compound assignment. The array has value semantics: mutating a copy or a
function parameter does not mutate its source.

An element assignment evaluates its indices once, from left to right, validates
the designation, and captures the old element for compound assignment before
evaluating the RHS. It applies the replacement to the current containing array,
preserving unrelated writes performed by the RHS. The assignment expression
returns the assigned element. Arrays must be initialized as whole values before
element mutation; partial initialization of an uninitialized array is not modeled.
Logical compound assignments, including array elements, short-circuit the RHS.

A block's final expression is its value only when it has no trailing semicolon.
Empty blocks and blocks ending in statements have no value. A direct function
body may use `-> expr;` or `->;`; later statements are structurally parsed but
not activated. Returns inside nested branches and iteration bodies transfer to
the enclosing function; completion properties retain their payload and state.

## Calls and control

Parameters without annotations are specialized from argument facts. A call
with concrete arguments may resolve completely; one with runtime arguments
can retain residual operations through the same function schema.

For overloads, exact type matches score above compatible checked/widening
conversions, which score above generic parameters. Scores are summed across
parameters; equal best scores are ambiguous. An exact integer representable in
both `#u8` and `#u16` does not prefer either. Return types never select targets.

```vexel
&choose(flag) { flag ? 42 : unavailable_function() }
^answer:#u8 = choose(1);          // valid: only the first arm is demanded
```

A known condition activates one arm. A runtime condition activates both, which
must have the same type (an exact integer arm can be checked against a concrete
partner). `condition ? statement` has no value. Logical operators short-circuit;
their conditional state and effects are retained even if their value becomes
constant.

Every internal member of a recursive call component requires `&~`. Runtime
recursion uses invocation backedges and closed return interfaces, including
base-path return type inference. Changed specialization arguments and mutated
captures trigger a graph revision. Invariant arguments retain their known
values. The MSX exporter materializes these interfaces within the caller's
banked module and gives recursive activations automatic storage. Constant
invocations can still resolve without emitting a runtime call.

## Arrays and iteration

Explicit representation casts support `#b[N]` to/from `#uN` with exact bit-count
matching, plus concrete scalars/composites to/from byte arrays with exact byte
counts. Boolean bit zero is the least significant integer bit. Canonical bytes
are little-endian, concatenate fields/elements without alignment padding, and
store Boolean elements as individual 0/1 bytes. Odd-width integer encodings
zero unused high bits; decoding discards those bits. Invalid Boolean bytes fail
at compile time, reject a finite candidate, or trap at runtime. Type queries
validate layouts without evaluating cast operands. MSX projection performs the
layout transfer explicitly instead of copying the host or SDCC struct bytes.

Array literals infer their shape and a common element type. An empty literal
gets its element type from a typed use site. `a..b` creates an ascending or
descending array, excluding the second bound. Bounds must be constants;
equal bounds are rejected (write `[]` instead).

`array@body` clones a body for each element and binds read-only `_`. Array
elements may depend on runtime values, provided the shape is known.
`array@@body` sorts a constant copy first. Nested loops can shadow `_`.
Iteration does not produce a value.

Runtime iteration uses `array!@body`, evaluates the iterable once, and visits
that captured value even when the original local array is subsequently changed.
Its element type must be concrete; for example, `(#u16)0..10!@body` gives a
concrete range. It creates one graph region, regardless of the number of elements.

`(condition)@body` repeats through worklist expansion while the condition is
known true. The condition is re-resolved against the state before each iteration.
An unknown condition is an error directing the author to `!@`. Expansion is
bounded by `--max-elements` (default 16384), node and worklist budgets.

`(condition)!@body` preserves a runtime loop. The condition executes before
every iteration, including its local writes and external effects. Parentheses
select the condition form; use a named array when parenthesizing an iterable
would be ambiguous. Neither repeat form introduces `_`.

Runtime loops may nest and may contain conditionals, calls, arrays, and unrolled
loops. Locals assigned across iterations currently require concrete types and
initial values before entering the loop. A conservative syntactic write footprint
selects carried slots; untouched locals retain their facts. This is not a
fixed-point constant-propagation analysis. Proven zero-trip loops omit their
bodies. Otherwise `!@` remains residual even for known arguments; `--call` does
not execute such a loop in the host resolver.

Loops and possible runtime bounds/division/shift traps carry a `control` effect.
Their result can be known while their execution must remain. In particular,
`(1)!@{}; 42` cannot become a function that simply returns 42. Constant global
initializers cannot contain unresolved runtime control. Break, continue and
nested returns carry separate completion properties, as described below.

```vexel
&digits() {
    n:#u32 = 0;
    [9, 1, 5]@@{ n = n * 10 + _; };
    n                             // 159
}
```

Arrays can flow between internal functions. Exports can contain constant array
data, but top-level arrays are not supported as exported function parameters or
results.

## Resources and external boundaries

`::assets::file.bin` in expression position loads a binary-safe `#s` snapshot.
Paths first search relative to the root source's directory, then relative to
the source containing the reference. Module paths similarly map to `.vx`
files. Resources are demanded lazily; source imports are eager.

Directory resources produce a sorted array of `(filename, contents)` tuples,
one per regular file, without recursion. Empty directories have a zero-length
tuple array type. An absent extensionless path denotes an empty directory;
an absent path with an extension is a missing-file error. Directory listing
and file contents are immutable snapshots across rejected solver revisions.
A type query lists names to establish the shape without reading contents.

`::"command" -> name;` declares a lazy process import. Demanding its value
requires `--allow-processes`; type queries and unused imports do not run it.
Commands run through `/bin/sh` in the importing source's directory, with a
bounded output size and `--process-timeout MS` (default 1000). Results and
failures are cached across revisions and recorded in JSON `process_inputs`.
Process side effects are outside language rollback. Their input dependencies
are not inferred: the project must supply those Make prerequisites explicitly.

`--depfile` includes source files, files actually read, directory listings and
the nearest existing parent of an absent directory. A bundled standard library
is still pending.

Runtime exported parameters are integers, Booleans or concrete named records.
An exported result must be concrete or have no value. External function
parameters also accept constant strings; external returns accept integers,
Booleans, concrete named records, or no value. Aggregate C inputs use const
addresses and results use output addresses. String C arguments use a byte
address and a length, preserving embedded NUL bytes.
Calls remain residual and observable. A constant return after an
external call still has an external effect.

## Remaining RFC coverage

Records, tuples, field and method probes, type queries, optional blocks,
nested functions with captures, expression parameters, receiver methods and
broadcast operators and bundled math are implemented as described below.
General cyclic type equations remain open beyond the alias joins and
initializer/argument-anchored closure described below.
Some independent type-demand cases
still need work beyond the supported recursive return equations.

Type expressions have their own dependency projections. For example,
`&f(n:#u8,unused:#u8){data:#u8[n]=[1,2,3];data[0]}` has an available result
type in `#[f(3,uninitialized)]`. Resolving its dimension reads `n`, including
reaching assignments and arithmetic that determine it, without evaluating the
unrelated argument. Array length can be obtained from shape without reading
elements. Inline helper calls and ordinary block results also project the
specific values used by the dimension. Actual execution of `f` and its helpers
still evaluates arguments and statements in source order. Calls with explicit
exits, materialized/recursive calls, and control-flow merges without a supported
independent projection retain their ordinary obligations; these projections do
not guess missing state. Projected record fields preserve the boundary between
an argument's evaluation and reading the copied field, including its invariant
checks and the original argument's single evaluation.

`::std::math;` provides the numeric interface specified in the RFC: absolute
value, min/max, floating square root, directed and nearest rounding, sign and
classification, integer square root, remainder, and integer powers. Unary calls
lift over arrays; binary calls use trailing-dimension broadcasting. Operands
evaluate once even for an empty result. Only the bundled fallback's declarations
receive these semantics; a project override replaces the entire module.
Floating roots and rounding use integer arithmetic for deterministic IEEE
f16/f32/f64 results. MSX projects include a separate banked `vx_math.c` helper
only when needed, supporting runtime f16/f32 and integer/fixed formats up to
32 bits. Negative integer square roots retain runtime trap 10 even when their
result is discarded. Finite relation failures use a structured numeric-trap
category, distinct from invalid syntax, missing bindings, and budget exhaustion.
Unresolved proofs, search exhaustion, observable ambiguity, and invalid heuristic
replay also have explicit diagnostic categories. Neither source identifiers nor diagnostic
wording control backtracking. Resource exhaustion cannot eliminate a candidate
or make an optional block disappear; `--max-bits` controls the integer bit budget.

Fixed point `#iI.F` and `#uI.F` uses arbitrary-width scaled integers during
resolution. Arithmetic, explicit rescaling casts, raw-bit operations, canonical
bytes, aggregates and finite choices use the same numeric rules. Products and
quotients truncate toward zero after full-width intermediate arithmetic; final
storage wraps. Reports display exact decimal values. MSX runtime fixed-point
storage supports widths up to 32 bits, using two-limb multiplication/division
helpers rather than host floating-point operations. C ABI values carry the raw
scaled integer, so hand-written C or assembly must use the declared fraction.

IEEE `#f16`, `#f32` and `#f64` source operations use exact integer ratios followed
by rounding to the selected format. Tests cover subnormals, signed zero, NaNs,
infinity, casts, aggregates and finite choices. MSX runtime `#f16` and `#f32`
use banked integer-only software arithmetic; C interfaces carry unsigned raw
IEEE bits. Runtime `#f64` exceeds this target's supported scalar width.

Imports establish declarations before expressions in their lexical block are
resolved. Constants retain lazy dependency-based resolution, including forward
references. Repeated and cyclic imports share one declaration set per scope;
distinct sibling scopes have distinct semantic bindings. Imported declarations
do not become visible outside the importing scope. Parsing still examines all
discovered source modules: optional blocks may discard missing imports, but
cannot discard malformed source. Optional absence uses diagnostic categories,
not diagnostic wording; missing resource files and invalid numeric widths are
ordinary errors.

`::std::bits;` provides the four IEEE reinterpretation helpers through ordinary
Vexel representation casts. The project-local `std/bits.vx` replaces the whole
bundled module. All `std/*` paths search the project root and bundled library;
they never search relative to an importing subdirectory.

Opaque `[[name]]` and `[[name(arguments)]]` metadata is preserved in JSON and
generated `annotations.json`. Annotation arguments are identifiers, strings
or numeric literals. Parenthesize an array expression when annotating it to
distinguish metadata from nested array syntax. Qualified function names parse
and bind their final component in the current flattened namespace.
`--strictness 1` requires explicit local and global variable types; `--strictness 2` also
requires concrete types at unconstrained value-parameter call boundaries.

The [MSX NEO-8 backend](backend-msx-neo8.md) exports an executable subset to C
for SDCC + megalinker. Its runtime values include integers and fixed point up to
32 bits, IEEE binary16/binary32, and Booleans; internal records, tuples and fixed arrays support runtime construction,
copies, mutation, indexing, conditionals, and iteration, including nested shapes.
Named records are supported at public function boundaries. Internal materialized
functions also accept and return arrays and tuples, and carry explicit captured
locations and receiver offsets. Generated C uses read-only aggregate input
addresses and caller-owned result addresses; parameter stores preserve Vexel
copy semantics. Deep comparisons and exported composite constants are supported.
Public constant symbols reside in the fixed resident region and consume its
checked memory budget. Wider
compile-time arithmetic remains available when it resolves before emission.
Backend-specific target facts are reserved bindings only when this target is
selected. Memory mapper allocation, automatic module
partitioning, general physical constraint propagation and stack-depth proofs
are not implemented.

Unsupported structural forms receive parse errors, including in dead code.
Unsupported type names are diagnosed when their type obligations are demanded.
This prototype does not claim RFC compatibility beyond this profile.

Transforming record and tuple equations can close individual leaf properties
without first settling the whole captured schema. A query of a nested field
uses original annotation projections and initializer witnesses in isolated
candidate contexts. The shared vector can grow when an overload reveals another
field equation; every witness is checked again after a revision. Only paths
whose original equations validate exactly are published. Unused siblings and
schema captures remain pending, including across later C++ API root demands.
JSON exposes these proofs as `type_properties`, with owner, path, witness,
check node, candidate and verification status. This is a checked common-type
closure, not an exhaustive search over arbitrary nonmonotone type equations.

## Joint finite constraints

`?{...}` resolves a pure finite constraint region. `?(#u8,1..10)` creates an
integer choice; `?(#u8[81],1..10)` creates 81 independent choices. `?(condition)`
requires a Boolean condition on the current path. The final expression is the
result, and all satisfying assignments must agree on that result. Hidden
choices may differ. Known globals, target constants, pure calls, exact and
wrapping arithmetic, arrays and record fields can participate.

Relations use the existing numeric rules, finite-domain propagation and a
reversible decision trail. Conditional requirements and scalar arithmetic are
guarded. Search distinguishes a unique observable answer, ambiguity, no
solution, unresolved obligations and exhausted budgets. An unfinished relevant
alternative prevents a unique answer; established constraints may still refute
that alternative. Two distinct completed answers establish ambiguity.
`--search-report FILE` records search statistics
and decision/conflict/backtrack events even when resolution fails.
`--branch-order forward|reverse|random`, `--max-decisions` and `--max-checks`
control finite search. `examples/sudoku.vx --root solution` requires no special
Sudoku implementation.

Symbolic array indices and updates are relations, including nested arrays,
records and constant strings. Invalid indices eliminate their guarded candidate.
Finite choices can determine types/shapes and unrolled loop bounds through
isolated [structural revisions](structural-search.md). General physical search
remains outstanding; the MSX experiment now resolves a bounded candidate policy
from measured costs using ordinary Vexel constraints. Optional compiled relation filters
are described in [compiled-search.md](compiled-search.md). Relation tables and work are bounded; an expansion-limit error
does not establish that a puzzle is impossible.

## Independent type and record properties

`#[expression]` demands type information independently of value and effects.
It can query an uninitialized typed local, an external signature, a generic
function body, and expressions whose evaluation would trap. A dimension or a
type-selecting condition may still require a compile-time value.

A dimension's value projection can cross a branch, loop or call without running
it when a conservative source analysis proves its automatic location unchanged.
The proof follows callees and implicit operator/iteration methods, canonicalizes
receiver aliases and examines expression arguments. Actual writes retain their
state dependencies. Nested declarations whose complete callable environment is
not available, unresolved callees and exits keep the ordinary dependency.
Recursive calls demand their declared return annotation as an independent
interface equation, including queries with frozen lexical schema parameters.
This does not make ordinary function evaluation skip conditions or arguments.

Array literals expose their length independently of their common element type.
An element-type join remains a separate obligation; shape helpers can consume
the length without resolving that join. Indexing or using a complete array type
requests the element interface, and actual array evaluation still evaluates all
elements. Element completion is checked before assuming a normally constructed
array when an operand can exit. Identity type conversions preserve pending
properties; value conversions close them. Empty broadcast values close their
element prototype's type without executing it. JSON reports list these promises
as `element_type_obligations`.

Conditional records, tuples and equal-length arrays expose separate property
joins when both branches complete normally without exits. Equal field types and
array extents are available without reading the condition. Other fields may
remain unresolved, have different types, or depend on unavailable values. A
queried field whose types differ still needs the condition; finite conditions
use ordinary structural search and observable-uniqueness checks. Nested joins
preserve this separation through records and arrays. Captured schema identity
has its own obligation, and resolving it preserves the selected schema origin.
Whole-interface queries, actual values and possible exits retain their ordinary
requirements. These type projections do not execute external conditions on the
host or emit them as part of a type-only query's runtime code.

`#Name(field[:Type],...);` declares a nominal record, `#Name(values...)`
constructs one, and `.field` selects a field. Unannotated fields specialize from
arguments. Records and arrays retain value-copy semantics through nested field
and element updates. `(a,b)` constructs a tuple with `.__0`, `.__1`, etc.; tuple
return annotations use `(#Type,#Type)`. Local type declarations are available
throughout their block. A known record field type can be queried while another
field's type remains unresolved. Generic record annotations retain independent
field obligations. Schema identity has a separate obligation: a query such as
`#[make(uninitialized).count]` can obtain the declared count type without reading
an invariant's unrelated lexical capture. Dependent array fields can likewise
provide their argument-supplied shape without evaluating their dimension guard.
Whole-record consumers and conversions close the required field and schema
obligations. Alias joins expose demanded properties independently; using their
whole type or a settled recursive interface still requires closure. JSON reports retain unresolved
`field_type_obligations` and `schema_obligations` explicitly. This does not yet
make every join or overload independent of unrelated schema information.
`a,b=expression` performs local tuple assignment with one
RHS evaluation and ordinary destination conversions, including receiver aliases.
Direct and indirect recursive storage declarations are rejected. Dependent
field annotations see every field through independent argument projections.
An argument supplies its array shape; dimension expressions contribute checked
equalities, including finite-search constraints and runtime guards. Constructors,
updates and representation casts preserve them. Tuple annotations and nested
record arrays follow the same rules. MSX reports a failed dimension guard as
trap 9. Internal records and arrays have been exercised through host C, SDCC
and the independent Z80 model.

Implicit aggregate conversions also preserve invariants. A wider field type
can change wrapping arithmetic inside a dimension relationship, so converting
function arguments, array elements or nested fields creates checked graph
obligations. Materialized calls consume those checked values. Each external
invocation retains its own resolved parameter-type facts; C emission rejects
incompatible signatures that share one external name.

Direct global `#[name]` alias components use initializer type joins, like direct
record field aliases. Declaration groups are complete before those joins run,
including imported declarations. This supports cyclic type aliases without
reading initializer values; explicit anchors retain their existing constraints.
It does not invent a type for an unanchored component or a value for a value cycle.

Direct alias groups join properties independently. A field query through
`#P(a:#[b],b:#[a]);` or global `a:#[b]=...; b:#[a]=...;` can establish that
field's common type without demanding other fields or captured schema values.
The join includes every supplied interface. Whole-type and value demands still
close the remaining properties; a later demand cannot treat a pending schema as
an empty schema. Installing an alias preserves the distinction between type
subscriptions and demands for a complete type descriptor.

Helper-mediated equality components also use these property joins. At a blocked
context, the resolver tests the original query with each unresolved owner replaced
by a distinct opaque type promise. It propagates ordinary interface rules in a
private graph, without candidate inference. Returning the same opaque promise
proves an equality; requiring its value or failing to establish equality leaves
the query to ordinary resolution. It does not recognize helper names or silently
assume their bodies preserve types. Typed overloads, narrowing conversions and
unresolved member probes cannot supply a false equality. Strictness-2 checks
for unconstrained literals also remain pending on an opaque argument; they
cannot use missing type information as evidence that no such literals exist.

The equality must remain within one complete lexical declaration or constructor
field group. Every initializer in the closed component contributes to its common
properties. No proof graph handles are exported. JSON `type_aliases` entries
identify the owning query, its equal target, and the property join; these record
an opaque-type equality proof, not a completed schema. Checkpoints preserve or
roll back this evidence with the graph, and proof propagation consumes the step
budget. A later whole-type or value demand still closes the schema and validates
actual constructor dimensions and conversions.

This supports cyclic record queries through generic forwarding helpers, nested
calls, expression parameters and tuple projections, with captured schemas or
unused fields still pending. Equations that transform records rather than prove
type equality still use the settled contextual candidate mechanism described
below; general demanded-property proofs for those equations remain unfinished.

Top-level query-annotated globals can also close cyclic expressions. For example,
`a:#[b+1]=(#u8)255; b:#[a+1]=(#u16)2;` resolves both interfaces to `#u16`.
At quiescence, demanded initializer interfaces seed a candidate vector. Each
original annotation runs through the ordinary solver in an isolated revision
under that same vector. Requirements widen candidates through the source
common-type rules. When no candidate changes, every original annotation must
equal its candidate. Temporary dispatch errors and arithmetic traps are retried
after widening; an exhausted budget remains exhaustion. Final validation nodes
are retained in the graph and listed as `type_equations` in JSON output.

Candidate changes restore the owning checkpoint, including dependent call
selection, values and dimensions. An optional or structural checkpoint cannot
revise interfaces already observed in its prefix. Optional success copies the
same solver context as other checkpoint commits; failure discards its candidate
checks and input dependencies. Revision work counts toward `--max-steps`.
Queries do not evaluate initializers solely to obtain their types; dimensions
that read a global value do use its candidate conversion and source arithmetic.

Scoped globals and constructed record fields use the same common-type closure.
For example, `#Pair(a:#[b+1],b:#[a+1]);` converts supplied `#u8` and `#u16`
arguments into two `#u16` fields. Each constructor and lexical module instance
keeps its own binding environment. Private candidate graphs start from the
blocked context and rebuild subscriptions for the proof, so unrelated value
roots cannot execute under tentative field types. Only validated candidates
are exposed through the still-open interfaces. Original annotation checks remain
graph obligations; field dimension guards still execute for actual values.

Finite choices discovered during equation closure return to their owning
structural checkpoint. Guarded inconsistencies reject their branch; distinct
observable results remain ambiguous, and exhaustion cannot eliminate a branch.
Recursive/early record returns and materialized MSX calls consume the checked
field interfaces. Separate instances may infer different widths from the same
source record or imported module without sharing a candidate.

Record-valued equations also use the contextual proof graph, including at the
top level. Nested records, tuples and arrays of records retain their candidate
schema origins when field types widen; handles created inside a private proof
never escape into the owning graph. Captured schema identities must agree.
Known annotation fields remain constraints when other fields need witnesses.
For example, `#P(a:#[#Box(b.x+1)],b:#[#Box(a.x+1)]);` with generic `#Box(x)`
and supplied inner `#u8`/`#u16` values establishes two `#Box(#u16)` fields.
The original queries validate that entire interface before consumers use it.

Type queries can inspect a known field of a partial global or constructor without
demanding unused siblings. An unresolved field promise remains an obligation;
only a genuinely unconstrained generic hole can adopt its supplied type directly.
Recursive return inference follows the constructor's converted field proxies,
so it cannot bypass these checks by taking the raw argument's narrower type.

Explicit global array annotations also participate in this closure. In
`a:#u8[|b|]=[1,2]; b:#u8[|a|]=[3,4];`, initializer shapes supply provisional
extents and the original dimensions validate them. Declared element families
remain authoritative. Nested dimensions and arrays of records retain generic
application context; original equations close genuine generic holes using the
supplied element interfaces. A type query can establish `#u8` without executing
an initializer trap or accepting an otherwise invalid narrowing conversion.
Actual value demand still performs those checks. Scoped module instances and
later root requests retain their own candidate and validation obligations.

Array common-type joins expose separate nested shape, field and schema
obligations. For example, the inner length of `[[missing()],[other()]]` can size
a type without demanding either leaf interface. A field query over an array of
records joins that field across every element; other fields and captured schemas
remain separate. Whole-type and value demands still close them and reject
incompatible schemas or leaf types. Generic initialization suspends on a pending
supplied property instead of treating its absence as an established mismatch.

This closure requires settled candidate properties and frozen record schemas.
General schema inference and arbitrary nonmonotone type search remain open.
Initializer conversions and original equations are checked; missing values and
unanchored cycles remain unresolved. The result reaches ordinary C/SDCC
projection without a separate backend type-inference policy.

Free lexical values used by a dependent invariant must be compile-time values.
They specialize the schema's identity and stay frozen when their original local
changes. Runtime dependencies must be explicit fields of the record. This also
applies to lexical captures of local functions called by an invariant. Capture
discovery uses ordinary non-evaluating overload selection when field/parameter
types establish its arguments, including forwarding through generic helpers.
Constructor-specific descriptors supply their generic field interfaces to this
discovery. They request only field promises needed by an overload question;
they do not execute constructor arguments to learn their types. The descriptor
keeps its original lexical state for freezing captures. Bare generic parameters
have separate descriptors for each call and overload candidate. Their argument
interfaces supply the same context through nested records, tuples and arrays;
the descriptor still freezes captures from its own lexical environment.
Different nominal candidates are eliminated before demanding their captures.
These descriptors participate in structural and optional rollback. A standalone
generic family annotation without an argument context can still require
conservative discovery across possible field specializations.
Initialized locals and globals, casts, and ordinary or early returns also supply
their interfaces to open requirements. Early-return annotations read resumed
returning state. Their interface projection joins return types without running
the returned values. Distinct promise handles first resolve their type
properties before a differing return interface can require a path condition.
Recursive generic result candidates use base-path interfaces and are checked by
the complete return and schema obligations. Generic slot specialization uses
type conversion; representability of exact values is checked when those values
are assigned.
Implicit unary, binary, compound and iteration methods participate in capture
discovery too. Dotted calls try their explicit overload before scalar methods;
the latter use element-type probes. Builtin fallback does not borrow captures
from an inapplicable method. Recursive materialization uses the same conservative
write analysis as property reads, so operator methods cannot silently preserve
a captured value as a specialization constant after writing it.
When a generic field or exact argument still lacks the information needed to
select a body, discovery remains conservative across possible candidates.
Invariants may use private scratch state, but cannot mutate enclosing storage.

`receiver.?field` queries field existence without evaluating the receiver.
`#{...}` validates an optional statement block in an isolated graph revision.
Success commits its graph/state; supported absence failures discard its values,
stores, declarations and finite constraints. Observed input dependencies remain
premises of the fallback, so changes can trigger a fresh Make resolution.
Discarded type-candidate revisions still retain their separate dependency sets.
Type mismatch, arithmetic errors, unresolved proofs and budget exhaustion are
not swallowed. This initially uses
an eager graph copy for isolation; it runs the same solver rules.


## Lexical calls, receivers and broadcasting

Nested functions have lexical callable bindings and capture enclosing slots by
reference. Their calls thread the captured state through the same store/load
relationships. They do not enter the value-only call cache. Expression
parameters use `$name`; arguments remain unevaluated until a use, each use gets
a fresh expression graph, and identifiers retain their caller bindings.
Unannotated unused expression arguments impose no value demand. Typed expression
parameters check their type and check concrete representability when used.
Expression parameters are restricted to inline internal functions.

Unary and binary operator methods use the receiver dispatch mechanism, with
builtin fallback when no method matches. Compound operators preserve a single
evaluation of the designation. Builtin scalar `&&` and `||` short-circuit;
applicable operator methods have ordinary eager value parameters. Dotted
operators lift scalar methods over private element copies, sequencing captured
writes in element order. Explicit dotted overloads take precedence.
`@` and `@@` methods receive one receiver and one expression argument;
they bind `_` before executing that argument. The sorted method requires a
compile-time receiver and supplies its own ordering. Builtin sorted iteration
uses applicable scalar `<` methods or builtin comparison, with stable ordering.
Custom comparisons must be pure and cannot modify enclosing storage.

## Completion and continuations

`-> expression;` and `->;` exit the current function, including from nested
blocks, branches and loops. `->|;` and `->>;` break or continue the innermost
enclosing loop. These are independent completion properties of graph regions;
they do not masquerade as normal expression values.

Blocks with explicit exits expose their next statement only while a normal path
remains. Later operands and arguments are deferred across a possible exit as
well, so their bindings are not constructed on an unreachable path. Expression
arguments retain the caller's lexical return and loop targets through forwarding.
A merge excludes an exited branch when reading continuing local state.
Functions join explicit return payloads with reachable tail values, and calls
consume only their own return exits. A captured mutation made before a return
remains visible to its caller. Loops consume their own break/continue exits and
forward returns. This applies to unrolled, repeated and runtime loops. The C
projection uses labels for inline returns and loop exits.

Type-only return queries use separate completion properties and do not read
return payload values. Candidate-dependent return types can require control
facts before their type is settled. Loops containing exits have separate
completion-type projections, allowing signature queries without executing
external calls or reading uninitialized payloads. Value-dependent type selection
still demands the values needed to distinguish types.

Receiver methods support direct, field, indexed and temporary receivers, as well
as multiple receivers. Designations are evaluated once, left to right, before
arguments. Receiver bindings identify a root slot and fixed index obligations;
o universal address is introduced. Overlapping receiver paths observe the same
updated state. A temporary receiver receives a private copy.

Method probes resolve name, receiver/argument types and overload shape without
executing arguments or the body. Exact integer expressions whose values have
not been demanded match numeric parameter families for this shape query;
value representability remains an obligation of an actual call. Multiple
compatible signatures remain ambiguous. Probe success does not prove that
evaluating an argument terminates or avoids a trap.

Dotted operators expand through ordinary indexed graph operations with trailing
shape alignment, singleton expansion and scalar lifting. Operands are evaluated
once. Dotted logical operators evaluate both operands. Dotted compound stores
preserve the ordinary designation and RHS evaluation rules. The backend reuses
its existing array/value projection for these operations.

## Optional search provider

`--heuristic PROGRAM` selects a bounded executable that can propose a finite
variable/value branch or reorder a requested finite structural decision. It
cannot assert a fact or remove alternatives. Invalid, stale, missing and
timed-out responses fall back to ordinary search. Numeric and structural
requests share a per-puzzle budget that persists across rejected revisions.
`--search-report` records proposals, including a durable `advice` list of requests
from discarded revisions; `--heuristic-replay FILE` reuses exact
matching requests offline. See [the protocol](search-provider.md) for budgets,
validation and limitations. A model adapter can implement this protocol; no
particular model service or credentials are built in.
