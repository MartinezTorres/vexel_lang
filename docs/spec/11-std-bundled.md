# 11. Bundled Standard Library Semantics (`std`)

RFC references: [Name Resolution & Modules](../vexel-rfc.md#name-resolution--modules), [Runtime Semantics](../vexel-rfc.md#runtime-semantics)

Operational sources:

- `std/math.vx`
- `std/bits.vx`
- `std/print.vx`
- `frontend/src/transform/evaluator_call.cpp`
- `frontend/src/type/typechecker_expr.cpp`

## 11.1 Scope

This chapter specifies the included bundled `std` behavior shipped with the compiler.

It covers:

- module fallback/override behavior,
- frontend-recognized bundled semantics,
- backend mapping expectations for bundled surfaces.

## 11.2 Resolution Rule

For `::std::<module>` imports:

1. project-local `std/<module>.vx` if present,
2. bundled fallback `std/<module>.vx` shipped with compiler otherwise.

Frontend-recognized bundled semantics apply **only** to bundled fallback modules (`ModuleOrigin::BundledStd`), not to project-local overrides.

## 11.3 `std::print`

`std/print.vx` is ordinary library code.

- There is no frontend builtin evaluator for `print`.
- There is no special backend symbol remapping requirement beyond normal external call handling.

## 11.4 `std::math` Surface

Bundled `std/math.vx` exports scalar extern surfaces including:

- `f64` family: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `exp`, `log`, `log2`, `log10`, `floor`, `ceil`, `trunc`, `round`, `fabs`, `sqrt`, `pow`, `atan2`, `fmod`, `isnan`, `isinf`, `isfinite`
- `f32` family (suffixed): `sinf`, `cosf`, `tanf`, `asinf`, `acosf`, `atanf`, `expf`, `logf`, `log2f`, `log10f`, `floorf`, `ceilf`, `truncf`, `roundf`, `fabsf`, `sqrtf`, `powf`, `atan2f`, `fmodf`, `isnanf`, `isinff`, `isfinitef`

### 11.4.1 Compile-Time Folding

When the call targets bundled `std/math.vx` and arguments are compile-time evaluable:

- frontend evaluator folds supported scalar calls,
- parameter coercion follows declared parameter types,
- predicate/classification functions return `#b`.

If the same surface is provided by project-local override, no bundled folding path is used.

### 11.4.2 Array Lifting and Broadcasting

Bundled `std::math` supports frontend rewrite for array-shaped calls:

- arity must be unary or binary,
- at least one argument must be array-shaped,
- all involved array sizes must be concrete compile-time sizes,
- arguments must be side-effect free,
- shapes must be broadcast-compatible.

Broadcast rule is strict trailing-dimension broadcasting with singleton expansion and scalar lifting.

Incompatible shapes are compile-time errors.

## 11.5 `std::bits` Surface

Bundled `std/bits.vx` defines explicit reinterpret surfaces:

- `f32_as_u32(x:#f32) -> #u32`
- `u32_as_f32(x:#u32) -> #f32`
- `f64_as_u64(x:#f64) -> #u64`
- `u64_as_f64(x:#u64) -> #f64`

It also defines signed convenience wrappers (`f32_as_i32`, `i32_as_f32`, `f64_as_i64`, `i64_as_f64`) as ordinary Vexel functions that compose the four primitive reinterpret APIs.

### 11.5.1 Compile-Time Folding

For bundled `std/bits.vx` primitive reinterpret calls:

- frontend folds using bit-preserving reinterpret semantics,
- conversion is not numeric conversion,
- argument and result type constraints must match the declared primitive surface.

Project-local overrides do not use bundled builtin folding semantics.

## 11.6 Backend Mapping Expectations (C-Generating Backends)

For bundled surfaces, C-generating backends currently map:

- bundled `std::math` externs to libc `<math.h>` symbol names (including f32 suffixed forms),
- classification helpers (`isnan`, `isinf`, `isfinite`) through macro-safe handling,
- bundled `std::bits` reinterpret calls to explicit bit reinterpret lowering (for example `memcpy`-style bit copy) instead of regular external symbol emission.

Non-bundled overrides are emitted as ordinary external/library calls per normal backend rules.

## 11.7 Diagnostics

Expected compile-time diagnostics include:

- array-lift shape mismatch for bundled `std::math`,
- array-lift side-effecting arguments,
- unsupported arity for bundled array-lift path,
- invalid primitive types for bundled `std::bits` reinterpret API usage.

## 11.8 Conformance Guidance

Semantics in this chapter are guarded by frontend tests under module/architecture/error suites that cover:

- bundled-vs-override behavior,
- array lifting and broadcast constraints,
- compile-time folding correctness,
- runtime lowering surface consistency.
