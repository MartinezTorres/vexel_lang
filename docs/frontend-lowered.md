# Lowered Vexel & Annotations (Stage 1 → Stage 2)

This document captures the staged pipeline plan and the annotation model now supported by the front-end.

## Staged pipeline
1. **Stage 1 (front-end)**: parse, type-check, monomorphize generics, evaluate compile-time expressions, prune dead code, and normalize to a lowered Vexel subset (no generics, explicit types, no expression parameters). Unknown annotations are preserved.
2. **Stage 2 (backends)**: consume the lowered subset and focus on target-specific emission (portable C and other backends). Annotations provide hints (e.g., reentrancy/placement/heat).

Use `-L/--emit-lowered` to write `<output>.lowered.vx` after Stage 1; backends still run on the in-memory module.

## Annotation syntax
- Prefix form, repeatable: `[[name]]`, `[[name(arg1, arg2)]]`, lists may be adjacent (`[[hot]] [[reentrant]]`).
- Parsed on: functions/methods, globals, type decls, params, fields, statements, expressions. Unknown annotations are passed through.

### Current recognition (warnings only)
- `[[hot]]`, `[[cold]]`, `[[reentrant]]`, `[[nonbanked]]` are recognized but only meaningful on functions (and `nonbanked` optionally on globals). Other placements emit warnings but are preserved. Recognized annotations are also preserved in the lowered output so backends can consume them.
- C backend: `[[hot]]`/`[[cold]]` map to GCC attributes on exported functions; reentrancy is derived from entry/exit annotations and propagated by analysis.
- Other backends: `[[nonbanked]]` may force globals into RAM; `[[reentrant]]` can be used for placement or call-graph constraints.

## Lowered subset (output contract)
- No generics or type variables; all types explicit.
- No expression parameters; bodies are fully specialized.
- No compile-time-only constructs remain; branches and constants are folded where possible.
- Imports/resources resolved; no implicit casts.
- Process expressions are executed via the host shell during lowering; only use this feature with trusted source input.
- Tuples lowered to concrete tuple types; return types explicit.
- Annotations preserved verbatim on all nodes.

## Normalized shape invariants
- Iteration (`@`, `@@`) and repeat bodies are always lowered as block expressions. If source uses a non-block body, lowering rewrites it to a block containing the expression as a statement.
- Iteration and repeat remain statement-only forms in lowered output (`type == null`), so consumers must not treat them as value-producing expressions.
- Blocks without a trailing value keep `result_expr = null`; there is no implicit unit/zero value in lowered form.
- Compile-time-known conditionals are collapsed to the selected branch before printing lowered output, so dead branches are absent from the lowered module.

The lowered printer (`print_lowered_module`) outputs a readable Vexel subset meant for Stage-2 consumption and debugging.
