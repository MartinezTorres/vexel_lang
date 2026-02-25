# Vexel Standard Library (bundled `std/`)

This directory contains the bundled Vexel standard-library modules used as the
fallback implementation for `::std::*` imports.

Resolution rule:
- The compiler first looks for project-local `std/<module>.vx`.
- If missing, it falls back to the bundled module in this directory.

The override unit is the module path (no symbol-level merge).

Current status:
- `std/math.vx` and `std/bits.vx` are placeholders that define a stable module
  namespace for future RFC/spec work and compiler-recognized functionality.
