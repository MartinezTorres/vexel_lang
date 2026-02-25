# Vexel Standard Library (bundled `std/`)

This directory contains the bundled Vexel standard-library modules used as the
fallback implementation for `::std::*` imports.

Resolution rule:
- The compiler first looks for project-local `std/<module>.vx`.
- If missing, it falls back to the bundled module in this directory.

The override unit is the module path (no symbol-level merge).

Current status:
- `std/math.vx` has a phase-1 scalar math surface backed by C `<math.h>` names.
  - `#f64` functions use the unsuffixed C names (`sin`, `sqrt`, `pow`, ...).
  - `#f32` functions use C-style suffixed names (`sinf`, `sqrtf`, `powf`, ...)
    because Vexel does not currently support function overloading.
  - The frontend can fold supported `std::math` calls at compile time when
    arguments are constexpr.
  - C-generating backends map bundled `std::math` externs to libc symbols
    instead of emitting mangled external names.
- `std/bits.vx` remains a placeholder namespace for future explicit bit
  reinterpretation APIs (`std::bits::*`).
