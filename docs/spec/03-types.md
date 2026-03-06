# 3. Type System

RFC references: [Types](../vexel-rfc.md#types), [Type Inference & Generics](../vexel-rfc.md#type-inference--generics), [Operations](../vexel-rfc.md#operations)

## 3.1 Primitive Families

- Signed integers: `#iN`, `N > 0`
- Unsigned integers: `#uN`, `N > 0`
- Fixed-point signed: `#iI.F`
- Fixed-point unsigned: `#uI.F`
- Floating: `#f16`, `#f32`, `#f64`
- Boolean: `#b` (values constrained to 0/1)
- String: `#s` (immutable compile-time constant)

Backend note:

- Frontend accepts arbitrary positive integer widths.
- Backends may support subsets natively and lower others explicitly.

## 3.2 Integer and Fixed-Point Rules

### Integer literals

- Integer literals start unresolved and become concrete by context.
- Representability is strict: values must fit target type exactly.

### Fixed-point

- `I + F > 0` required.
- Signed fixed uses sign bit within `I`.
- Bitwise/shift operate on raw stored representation.
- Fixed operators require exact operand type match (no implicit rescaling).

## 3.3 Floating Rules

- `#f16/#f32/#f64` are IEEE family types.
- Unconstrained float literals default to `#f64`.
- Casts are explicit for narrowing/widening changes across numeric families.

## 3.4 Arrays

- Type form: `#T[N]` where `N` is compile-time known non-negative integer.
- Nested arrays are ordinary composition: matrices/tensors are arrays of arrays.
- Array size participates in type identity (`#i32[4] != #i32[5]`).

Array literal behavior:

- Element type inferred by unification.
- If all elements are unresolved integer literals, element type remains unresolved until constrained.
- Once constrained, each element must be representable in that element type.

## 3.5 Record Types (`#Name`)

- Declaration: `#Name(field[:Type], ...);`
- Constructor expression: `#Name(args...)`
- Record recursion is disallowed (no self-reference fields).
- Records can be declared at module or block scope.

## 3.6 Generics and Monomorphization

- Omitted types in declarations create type parameters.
- Instantiations are monomorphized per use.
- Type-checking is performed per concrete instantiation path.

## 3.7 Cast Semantics

- Cast syntax: `(#T)expr`
- Cast is explicit and creates a value copy.
- No implicit narrowing.
- No implicit signed/unsigned conversion.
- Boolean conversion is explicit unless operation semantics explicitly require scalar-to-bool conversion.

## 3.8 Type Expression (`#[expr]`)

- `#[expr]` resolves to static type of `expr` in current scope.
- Uses current inference facts (including unresolved literal status until constraints arrive).
- Supports suffix composition like `#[seed][4]`.

Example:

```vx
seed:#u16 = 42;
arr:#[seed][4] = [10, 20, 30, 40];
out:#[seed] = arr[2];
```

## 3.9 Function Type Constraints

- Functions are not first-class values.
- No function-type variables or function-valued fields.
- Calls are direct/monomorphized invocation forms only.

## 3.10 Representability Contract

For assignment, initialization, argument passing, and return:

- concrete target type must be known where value use is semantically required,
- source value must be representable in target domain,
- unresolved values may flow only where language rules allow deferred inference.
