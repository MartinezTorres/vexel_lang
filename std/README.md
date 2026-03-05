# Vexel Standard Library (bundled `std/`)

This directory contains the bundled Vexel standard-library modules used as the
fallback implementation for `::std::*` imports.

Resolution rule:
- The compiler first looks for project-local `std/<module>.vx`.
- If missing, it falls back to the bundled module in this directory.

The override unit is the module path (no symbol-level merge).

Current status:
- `std/print.vx` provides an ordinary overloaded `print(...)` surface.
  - Supported directly: `#s`, `#b`, signed/unsigned integers through `#i64`/`#u64`.
  - Arrays recurse through a generic iterable fallback.
  - This is library code; there is no compiler builtin `print`.
- `std/math.vx` has a phase-1 scalar math surface backed by C `<math.h>` names.
  - Includes scalar math functions (`sqrt`, `sin`, `pow`, ...) and classification
    helpers (`isnan`, `isinf`, `isfinite`) for `#f64`, plus `f32` suffixed forms.
  - `#f64` functions use the unsuffixed C names (`sin`, `sqrt`, `pow`, ...).
  - `#f32` functions use C-style suffixed names (`sinf`, `sqrtf`, `powf`, ...)
    because ABI-visible external overloads are not currently supported.
  - Compiler-recognized math behavior applies only to the bundled fallback
    module in this directory (not to project-local `std/math.vx` overrides).
  - The frontend can fold supported bundled `std::math` calls at compile time
    when arguments are constexpr.
  - Array-shaped lifting is supported for bundled `std::math` calls:
    - unary functions accept arrays and lift element-wise
    - binary functions use strict broadcasting (trailing-dimension alignment,
      singleton-dimension expansion, scalar lifting)
    - incompatible shapes are compile-time errors (no implicit transpose,
      reshape, or flatten)
    - current implementation requires side-effect-free arguments because
      lifting expands to repeated indexed scalar calls without temporary
      materialization
  - C-generating backends map bundled `std::math` externs to libc symbols
    instead of emitting mangled external names; local overrides emit normal
    external symbols.
- `std/bits.vx` remains a placeholder namespace for future explicit bit
  reinterpretation APIs (`std::bits::*`).
