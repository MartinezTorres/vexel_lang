# VEXEL - Language Specification (v0.2.1)

## Preamble

Vexel: strongly typed, minimal, operator-based language with no keywords.

**No keywords**: All syntax via sigils and operators. No reserved words. Primitive type names (`iN`, `uN`, `f16`, `f32`, `f64`, `b`, `s`) are only special after the `#` sigil; they may be used as identifiers elsewhere.

**No standard library**: Language defines syntax and semantics only. External functions provide I/O and platform services.

**No error handling**: Language provides no explicit error handling mechanism (no exceptions, result types, or error codes). Runtime errors trap and exit.

**Whole-program compilation**: All source available at compile time. Aggressive optimization and dead code elimination. Only explicitly exported declarations are callable/visible externally (`&^` functions, `^` globals).

**Compilation model**: Backend specifications define target language, type mappings, calling conventions, code organization. Compiler can generate executables, libraries, or C source for integration.

---

## Lexical & Tokens

- **ASCII-7 only**. Whitespace: space, tab, CR, LF.
- **Identifiers**: `[A-Za-z_][A-Za-z0-9_]*` (no length limit)
- **Comments**: `//` to end-of-line
- **Literals**:
  - Integers: `123`, `0xFF`. Inferred as the smallest fitting integer type (see literal inference rules).
  - Floats: `1.23`. Default: `#f64`
  - Strings: `"..."` with escapes: `\n` `\r` `\t` `\\` `\"` `\xHH` (hex) `\NNN` (octal), no length limit
  - Chars: `'a'`. Default: `#u8`
- **Sigils**: `$` (expression param), `@` (iteration), `&` (function decl), `&!` (external function), `&^` (exported function), `^` (exported global), `!` (external global symbol), `!!` (backend-bound global), `#` (type)
- **Operators**: `+ - * / % & | ^ ~ << >> == != < <= > >= = -> ->| ->> . , ; : :: @ @@ ! && || ? ( ) { } [ ]` (user types may overload `+ - * / % == != < <= > >=` via operator methods)
  - Note: `|` serves as both unary (length/absolute) and binary (bitwise OR) operator
- **Statement termination**: `;` or any whitespace when unambiguous (see Section 8 for ambiguous parsing rules)
- **Annotations**: Optional prefixes `[[name]]` or `[[name(arg1, arg2)]]` may precede declarations, statements, and expressions. Annotations are opaque to language semantics; the frontend preserves them for backends/tooling.

---

## Types

**Primitives**:
- Parametric signed integers: `#iN` where `N` is any positive integer width
- Parametric unsigned integers: `#uN` where `N` is any positive integer width
- Floating point: `#f16`, `#f32`, `#f64`
- Backends may support only a subset of integer widths (for example, C/megalinker currently support 8/16/32/64)
- Boolean: `#b` (values 0 or 1 only, no true/false literals)
  - Use 0 for false, 1 for true
  - Arrays of booleans can be bit-packed: `#b[8]` fits in one byte
- String: `#s` (immutable, compile-time constant)
  - String length: `|str|` yields compile-time constant integer (number of bytes)

**Arrays**: `#T[N]` where N is a compile-time known non-negative integer.
- Size must be a non-negative integer resolvable at compile time
- Cannot depend on external calls, runtime values, or I/O
- Examples: `#i32[10]`, `#u8[2+3]`, `#f32[pow(2,3)]` (if pow is pure)
- Equivalent prefix spelling is accepted for a single dimension: `[N]#T`
- - Array length: `|arr|` yields compile-time constant integer (array size)
- Literal `[e1,e2,...]` infers size from element count (may be empty)
  - `[ ]` produces `#T[0]` for the target type `#T` when context requires an array
  - Type inferred as minimal type that fits all elements collectively:
  - **Note**: Individual literal type inference (e.g., 0→#b, 1→#b) does NOT apply inside arrays
  - Examine the full range of values: smallest to largest
  - Values 0-1 only → `#b`
  - Values 2-255 → `#u8`
  - Any negative value → smallest signed type that fits range
  - Examples:
    - `[0]` → `#b[1]` (only 0)
    - `[0,1]` → `#b[2]` (values 0-1 fit in b)
    - `[0,1,2]` → `#u8[3]` (value 2 requires u8)
    - `[0,1,255]` → `#u8[3]` (max value 255 fits in u8)
    - `[-1,0,1]` → `#i8[3]` (has negative, fits in i8)
    - `[0,1,256]` → `#u16[3]` (max value 256 needs u16)
  - Automatic promotion applies when assigning to typed arrays
- Size is part of type: `#i32[10]` != `#i32[20]`

**Type constructors**: `#Name(field[:Type],...);`
- Defines record type `#Name`
- Auto-generates constructor callable as `Name(args...)`
- Fields accessed via `.field`
- Can be defined at module level or within any scope
- **Cannot be recursive** (no self-reference in fields, no pointers)

**Generics**: Omitted param/field types become type parameters; monomorphized at use sites.

**No function types**: Functions cannot be stored in variables or passed as values. Function references only exist through direct calls or generic instantiation.

**Type annotation**: `x:#T`

**Type expressions**: `#[expr]`
- Resolves to the static type of `expr` in the current scope.
- Uses normal type inference rules (for example, `#[1]` resolves to `#b`).
- `expr` is type-checked only; `#[expr]` does not evaluate `expr` or trigger side effects.
- Must resolve to a concrete type before backend handoff.
- Valid anywhere a type is expected, including cast targets and declaration annotations.

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
  - Portable frontend compile-time cast support is currently bounded to integer widths up to 64 bits
  - Bit 0 is LSB, bit N-1 is MSB
- **Composite type casts**: Can cast between composites and byte arrays
  - Creates byte representation (portable across backends)
  - Size must match exactly in bytes
  - Backend handles layout conversion transparently

**Automatic promotion**: Numeric operations widen within the same family to the larger bit width:
- `#iA` with `#iB` promotes to `#i(max(A,B))`
- `#uA` with `#uB` promotes to `#u(max(A,B))`
- `#f16`/`#f32`/`#f64` promote to the wider float type
- No implicit narrowing
- No implicit signed/unsigned conversion (must use explicit cast)

**No void/unit type**: Functions without `->` infer return type. Empty return `->;` and statement conditionals have no type (not expressions).

**Memory model**: No heap allocation. All data is stack-allocated or compile-time constant.

**Literal type inference**:
- Integer literals use minimal semantic width, then normalize to backend-oriented buckets:
  - `0`, `1` → `#b`
  - unsigned literals normalize to `#u8` / `#u16` / `#u32` / `#u64`
  - signed literals normalize to `#i8` / `#i16` / `#i32` / `#i64`
  - Assignment/casts can target any explicit `#iN`/`#uN` width when values fit
- Floating literals: `#f64`
- Character literals: `#u8`

**Overflow behavior**: Integer arithmetic uses two's complement wrapping. Float behavior follows IEEE 754 standard.

---

## Declarations

**Global variables**:
- **Immutable (constants)**: `name[:Type] = expr`
  - Initialized with compile-time constant expression
  - Cannot be modified at runtime
  - Usable in compile-time expressions
  - Backend can place in ROM or optimize away entirely
  - Pure functions can compute initial values at compile time
  - **Initialized in parse order (top-to-bottom)**
  - Can only reference globals defined earlier in the file
- **Exported immutable global**: `^name[:Type] = expr`
  - ABI-visible symbol
  - Must be immutable and compile-time constant
  - Type must be ABI-safe data (`primitive`, fixed-size arrays of ABI-safe data, or named structs recursively composed of ABI-safe fields)
  - Tuple types, type variables, and unresolved type expressions are rejected at the ABI boundary
  - Exported globals are retained as ABI roots (not removed by dead-code elimination)
- **Mutable (variables)**: `name:Type`
  - No initialization at declaration
  - Can be modified at runtime
  - Contents undefined until first assignment
  - Backend typically places in RAM
  - Must be initialized before use (compiler may not enforce)
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
  - Frontend validates syntax only; selected backend validates whether annotations are sufficient to materialize the binding

**Declaration order**:
- No forward declarations needed (whole-program compilation)
- Functions can be defined anywhere and called from anywhere
- Type definitions can appear anywhere
- Global constants initialize in parse order
- During constant initialization, only previously defined globals are accessible
- At runtime, all symbols are accessible regardless of definition order

**Internal functions**: `&name(p[:Type],...)[-> Type] block`
- Returns last expression or `-> expr;` for explicit return
- Type inference when `->` omitted
- `->` is the only return operator (last expression = implicit return, `-> expr;` = explicit, `->;` = empty)
- Return exits only the current function (not enclosing functions)
- May be eliminated if unused
- Can be nested within other functions or scopes
- Nested functions automatically capture enclosing scope variables by reference
- Can access all globals (both mutable and immutable)
- Captured variables remain accessible throughout nested function's lifetime
- Nested functions cannot outlive their enclosing function (no function types)
- Nested functions can be recursive

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
- If the method mutates the receiver, the compiler passes it by reference; otherwise it is passed by value
- If a mutating receiver is not a mutable lvalue, the compiler materializes a temporary and applies mutations to the copy
- Compilers may specialize separate mutable/non-mutable receiver paths (duplicating code if needed) to preserve aliasing guarantees
- Receiver identifier is an lvalue; type annotation is optional and defaults to `TypeName`
- Parameters (non-receiver) passed by value
- `TypeName::` creates a method namespace (separate from general functions)
- Called via dot notation: `receiver.methodname(args)`
- Example: `&(v)Vec::push(item) { ... }` called as `v.push(42)`
- Methods and functions have separate namespaces: `&Vec::push` and `&push` don't conflict

**Operator methods**: `&(lhs)#TypeName::+(rhs[:Type])[-> Type] block`
- Overloads a core operator for instances of `TypeName`; the operator name must be one of `+ - * / % == != < <= > >=`
- Operator methods behave like regular methods: the left operand is the receiver and may be any expression; mutating operators may materialize a temporary for non-lvalues
- When the left operand has type `TypeName`, the compiler resolves `lhs <op> rhs` as a call to `TypeName::<op>` before attempting built-in operator rules
- Operator methods may not declare expression parameters and must accept exactly one receiver; value parameters model the remaining operands (binary operators expect one value parameter)
- Return types follow normal rules: arithmetic operators typically return `TypeName`, comparison operators return `#b`
- Absent an overload, built-in semantics apply (primitives retain their native arithmetic and comparison rules)

**Iteration methods**: `&(receiver)#TypeName::@($loop) block` and `&(receiver)#TypeName::@@($loop) block`
- Provide custom iteration semantics for `receiver@{...}` and `receiver@@{...}`. The method name must be `@` or `@@`, with exactly one receiver and exactly one expression parameter (the loop body).
- The compiler expands the method inline (functions with expression parameters are never emitted); the method must bind `_` before evaluating `$loop` for each element. Typical pattern: declare `_` locally, assign the current element, then evaluate `$loop`.
- `@@` methods are responsible for delivering elements in sorted order; when absent, the compiler falls back to built-in array sorting only if the iterable is a plain array. Named types without the appropriate method produce a compile-time error.
- The iterable expression may be any expression; if the iteration method mutates the receiver, the compiler may materialize a temporary.

**Reference-taking functions (multi-receiver)**: `&(receiver1, receiver2, ...)#TypeName::methodname(p[:Type],...)[-> Type] block`
- Receivers evaluate left-to-right
- Each receiver is treated like a receiver parameter: mutating receivers are passed by reference, non-mutating by value
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
- **Method call**: `receiver.fname(args)` (receiver passed by reference if mutating, otherwise by value)
- **Constructor**: `TypeName(args)`
- **Member**: `x.y`
- **Index**: `arr[expr]`
- **Block**: `{ stmt; ... }` evaluates statements, yields last expression
- **Conditional (expression)**: `cond ? expr_true : expr_false`
  - Compiler attempts to evaluate cond at compile time
  - If successful (compile-time constant), branches can have different types (dead branch eliminated)
  - If not (runtime value), branches must have same type
  - Everything is compile-time unless it cannot be
  - Nested conditionals without parentheses are a compile error
  - Example: `a ? b : c ? d : e` is an error (must use parentheses)
- **Conditional (statement)**: `cond ? stmt` (no else branch, not an expression, has no type or value)
- **Iteration**: `iterable@expr` binds `_` to each element; `iterable@@expr` binds `_` to each element of a sorted copy
  - `iterable` evaluates once before the loop starts; iteration walks the resulting array value (arrays and ranges only)
  - `@@` copies the evaluated array, sorts it using the comparison rules in §2, then iterates over the sorted copy (the original array is never mutated)
  - Compile-time arrays may be fully sorted at compile time; otherwise backends sort with a stable algorithm before entering the loop body
  - `_` is scoped to the loop body, read-only, and only available in iterable loops
  - `0..10@print(_)` prints 0 through 9; `arr@process(arr[_])` visits each array element
  - `arr@@print(_)` visits the same elements but in lexicographic order
  - Named types can override iteration by defining `&(self)#Type::@($loop)` / `&(self)#Type::@@($loop)` (see Iteration methods above); without an override, only arrays and ranges are accepted
  - Nested loops shadow `_` (innermost wins)
- **Process expressions**: execute host commands during compilation; implementations should default to disabling them and provide an opt-in flag (e.g., `--allow-process`) to enable. Compilers that allow them MUST treat the input program as fully trusted; sandboxes should default to disabling process execution for untrusted sources.
- **Repeat**: `(cond)@expr` repeats expr while cond is true
  - Condition re-evaluates before every iteration; no `_` is bound in this form
- **Loop control**: `->|;` (break), `->>;` (continue) - affect innermost enclosing loop
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
- Range (..)
- Shifts (<< >>)
- Multiplicative (* / %)
- Additive (+ -)
- Bitwise AND (&)
- Bitwise XOR (^)
- Bitwise OR (|)
- Relational (< <= > >=)
- Equality (== !=)
- Logical AND (&&)
- Logical OR (||)
- Conditional (?:)
- Assignment (=)
- `@` / `@@` (iteration/repeat) bind looser than assignment; parentheses required to combine with other operators

**Assignment**:
- Single assignment: `x = expr` returns the assigned value
- Can be used in larger expressions: `y = (x = 5) + 1` assigns 5 to x, 6 to y
- Evaluation is right-to-left for chained assignments: `a = b = c` evaluates as `a = (b = c)`
- **Multi-assignment**: `a, b, c = expr` where `expr` returns tuple type
  - Syntactic sugar: desugars to temp variable + individual assignments
  - Example: `q, r = divmod(10, 3)` → `__tmp = divmod(10, 3); q = __tmp.__0; r = __tmp.__1`
  - Number of variables must match tuple arity

**Parsing rules**:
- && binds tighter than ||
- Ambiguous expressions require parentheses
- Nested conditionals must be explicitly parenthesized
- Parser errors on ambiguity rather than guessing intent

## Annotations & Lowered Form

- Annotations are metadata prefixes: `[[name]]` or `[[name(arg1, arg2)]]`. Multiple may be stacked (e.g., `[[backend_hint]] [[schedule(io)]] &^foo(...)`).
- Placement: before functions/methods, type declarations, globals, statements, parameters/fields, and expressions.
- Unknown annotations are preserved verbatim; they do not change language semantics.
- Lowering is internal to the frontend pipeline. Backends consume the lowered, fully type-checked, monomorphized module contract; no frontend textual lowered output is part of the CLI surface.

---

## Name Resolution & Modules

- Single namespace per module (functions, variables, and types share namespace)
- Import: `::A::B;`
- Resource expressions: `::A::B::file.ext` used in expression context load the referenced resource at compile time
  - Files resolve using the same search order as modules (project root, then relative to the importing file)
  - A file evaluates to a `#s` (immutable string) whose bytes exactly match the file contents (binary-safe)
  - A directory evaluates to an array of tuples `(__0:#s, __1:#s)` (one per regular file, lexicographic order, non-recursive); `.__0` is the file name and `.__1` the file contents. Empty or missing directories yield an empty tuple array.
  - Process imports: `::"command" -> name;` execute `command` at compile time (via the host shell) and bind its captured stdout as an immutable `#s` named `name`
  - Resource expressions are immutable constants and may appear in initializers or expressions; attempting to mutate them is a compile-time error
  - Example: `font:#s = ::assets::font.bin;` embeds `assets/font.bin` as a string, while `sprites = ::assets::tiles;` embeds the contents of each file within `assets/tiles/`
- Symbols are internal by default; export explicitly with `&^` (functions) or `^` (globals)
- Resolution: `::a::b;` maps to `a/b.vx` (case-sensitive, relative to project root)
- Scoped imports instantiate their module once per lexical scope; instances do not share mutable state
- Re-importing the same module in the same scope is permitted only when every top-level definition is identical (functions/types textually equal; constants equal after compile-time evaluation); otherwise it is a compile error
- Imports can be scoped to blocks
- Type definitions can be scoped to blocks (visibility limited to that scope)
- Function definitions can be scoped to blocks (nested functions)
- Nested functions capture enclosing scope variables by reference
- No import aliases
- No shadowing (compilation error if names conflict), except `_` in nested iterations
- **Initialization order**: Each module initializes after all of its imports have initialized; within a module constants run in parse order. Cycles are a compile-time error.
- **Forward references**: Functions and types can be used before definition, constants cannot

---

## Type Inference & Generics

- Hindley-Milner per function body
- Omitted parameter/field types create fresh type variables inferred from call sites
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
- Array sizes and global initializers must be compile-time constants
- Immutable globals (initialized at declaration) are compile-time constants
- Functions execute at compile time when needed for constants (no explicit purity annotation)
- **Initialization is sequential**: Each module's imports initialize first; within a module, constants run top-to-bottom
- **During initialization**: Can only reference previously defined constants
- **Compile-time execution constraints**:
  - Cannot call external functions (`&!`)
  - Cannot modify mutable globals
  - Can only read immutable globals defined earlier
  - All paths must be deterministic (no runtime-dependent branches)
  - Accessing uninitialized values triggers compile-time error
- **Purity determination**: Compiler attempts compile-time execution and fails if constraints violated
- Compile-time execution is backend-independent:
  - Can use any type (even unsupported by target)
  - Results must be convertible to target types
  - Example: use #f64 math to build #u8 lookup table
- Compile-time conditionals eliminate dead branches before type checking
- No external calls, I/O, or side effects during compile-time execution

**Memory**:
- Primitives by value
- Composites: value semantics (assignment copies)
- Assignment always creates independent copy (no aliasing)
- Backend may optimize to copy-on-write or move when safe
- No heap
- Stack or compile-time constant
- All mutable state is function-local (except through references)
- Lifetime: end of scope

### Operations
- Arrays: `arr[i]` indexing
- Strings: indexing yields `#u8` (read-only, cannot modify string contents)
- Equality: value-based (deep comparison)
- Assignment: always copies (no aliasing)
  - `b = a` creates independent copy of a
  - Modifying b doesn't affect a
  - Backend optimizes unnecessary copies
- Division by zero: runtime trap
- Modulo operator (`%`): unsigned integers only (well-defined result)
- Bitwise operators (`&`, `|`, `^`, `~`, `<<`, `>>`): both operands must be unsigned integers
- Length operator `|expr|`: arrays must declare a compile-time constant length; strings are allowed dynamically, other operand types are rejected
- Shift amount must be less than type bit width (undefined otherwise)
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

```
program      ::= { top }

top          ::= func | export | extern | type_decl | import | global

func         ::= '&' [ recv_prefix ] func_name '(' [ params ] ')' [ '->' ret_spec ] block
export       ::= '&^' [ recv_prefix ] func_name '(' [ params ] ')' [ '->' ret_spec ] block
extern       ::= '&!' [ recv_prefix ] func_name '(' [ params ] ')' [ '->' ret_spec ] ';'

recv_prefix  ::= '(' refparams ')' [ '#' ident '::' ]
func_name    ::= ident | op_name
ret_spec     ::= type | '(' type ',' type { ',' type } ')'
op_name      ::= '+' | '-' | '*' | '/' | '%' | '==' | '!=' | '<' | '<=' | '>' | '>=' | '@' | '@@'

params       ::= param { ',' param }
param        ::= [ '$' ] ident [ ':' type ]
refparams    ::= ident { ',' ident }

global       ::= [ '^' ] ident [ ':' type ] '=' expr    // immutable constant
             |   ident ':' type                          // mutable variable
type_decl    ::= '#' ident '(' [ fields ] ')' ';'
fields       ::= ident [ ':' type ] { ',' ident [ ':' type ] }

import       ::= '::' qname ';'

block        ::= '{' { stmt } [ expr ] '}'
stmt         ::= expr ';' | '->' [ expr ] ';' | '->|' ';' | '->>' ';' | expr '@' expr | '(' expr ')' '@' expr | expr '?' stmt | type_decl | import | func | refunc

expr         ::= assign
assign       ::= cond | (lvalue '=' expr)
cond         ::= logic_or ( '?' expr ':' expr )?
logic_or     ::= logic_and { '||' logic_and }
logic_and    ::= bit_or { '&&' bit_or }
bit_or       ::= bit_xor { '|' bit_xor }
bit_xor      ::= bit_and { '^' bit_and }
bit_and      ::= compare { '&' compare }
compare      ::= shift { ('=='|'!='|'<'|'<='|'>'|'>=') shift }
shift        ::= range { ('<<'|'>>') range }
range        ::= sum [ '..' sum ]
sum          ::= prod { ('+'|'-') prod }
prod         ::= unary { ('*'|'/'|'%') unary }
unary        ::= postfix | ('-'|'!'|'~') unary | '|' unary '|' | '(' type ')' unary
postfix      ::= primary { call | index | member }
primary      ::= literal | var | '(' expr ')' | block | array | funccall | methodcall | constructor

var          ::= ident [ ':' type ]
funccall     ::= qname '(' [ arglist ] ')'
methodcall   ::= (ident | '(' ident { ',' ident } ')') '.' ident '(' [ arglist ] ')'
constructor  ::= qname '(' [ arglist ] ')'
call         ::= '(' [ arglist ] ')'
index        ::= '[' expr ']'
member       ::= '.' ident
arglist      ::= expr { ',' expr }
array        ::= '[' [ expr { ',' expr } ] ']'

qname        ::= ident { '::' ident }
ident        ::= [A-Za-z_][A-Za-z0-9_]*

type         ::= '[' expr ']' '#' ( ident | '[' expr ']' )
               | '#' ( ident | '[' expr ']' ) { '[' expr ']' }

lvalue       ::= var | methodcall | index | member

literal      ::= number | float | string | char
number       ::= [0-9]+ | '0x' [0-9A-Fa-f]+
float        ::= [0-9]+ '.' [0-9]+
string       ::= '"' ( escape | [^"\\] )* '"'
char         ::= "'" ( escape | [^'\\] ) "'"
escape       ::= '\\' ( [nrt\\'"] | 'x' hex hex | [0-3] [0-7] [0-7] )
hex          ::= [0-9A-Fa-f]

line_comment ::= '//' .*
```

**Calls**:
- `fname(...)`: function call (internal or external)
- `receiver.fname(...)`: method (receiver by reference if mutating, otherwise by value)
- `(r1, r2).fname(...)`: multi-receiver method
- `TypeName(...)`: constructor
