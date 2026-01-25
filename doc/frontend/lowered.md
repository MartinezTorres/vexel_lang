# Lowered Vexel & Annotations (Stage 1 → Stage 2)

This document captures the staged pipeline plan and the annotation model now supported by the front-end.

## Staged pipeline
1. **Stage 1 (front-end)**: parse, type-check, monomorphize generics, evaluate compile-time expressions, prune dead code, and normalize to a lowered Vexel subset (no generics, explicit types, no expression parameters). Unknown annotations are preserved.
2. **Stage 2 (backends)**: consume the lowered subset and focus on target-specific emission (C portable, banked SDCC, etc.). Annotations provide hints (e.g., reentrancy/placement/heat).

Use `-L/--emit-lowered` to write `<output>.lowered.vx` after Stage 1; backends still run on the in-memory module.

## Annotation syntax
- Prefix form, repeatable: `[[name]]`, `[[name(arg1, arg2)]]`, lists may be adjacent (`[[hot]] [[reentrant]]`).
- Parsed on: functions/methods, globals, type decls, params, fields, statements, expressions. Unknown annotations are passed through.

### Current recognition (warnings only)
- `[[hot]]`, `[[cold]]`, `[[reentrant]]`, `[[nonbanked]]` are recognized but only meaningful on functions (and `nonbanked` optionally on globals). Other placements emit warnings but are preserved. Recognized annotations are also preserved in the lowered output so backends can consume them.
- C backend: `[[hot]]`/`[[cold]]` map to GCC attributes on exported functions; `[[reentrant]]` removes a function from the non-reentrant set used by backends.
- Banked backend: `[[nonbanked]]` forces globals into RAM; `[[reentrant]]` is preserved for future alternation analysis and placement.

## Lowered subset (output contract)
- No generics or type variables; all types explicit.
- No expression parameters; bodies are fully specialized.
- No compile-time-only constructs remain; branches and constants are folded where possible.
- Imports/resources resolved; no implicit casts.
- Process expressions are executed via the host shell during lowering; only use this feature with trusted source input.
- Tuples lowered to concrete tuple types; return types explicit.
- Annotations preserved verbatim on all nodes.

The lowered printer (`print_lowered_module`) outputs a readable Vexel subset meant for Stage-2 consumption and debugging.***
