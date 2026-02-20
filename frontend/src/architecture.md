# Frontend Architecture Contract

This file is the frontend source of truth for semantic ownership and invariants.
If code and this document diverge, code must be changed to match this contract.

## Goal

Vexel frontend owns language semantics, including:

- Compile-time execution (CTE)
- Dead-code elimination (DCE)
- Whole-program graph analysis
- Type soundness of all reachable value uses

Backends consume the analyzed/lowered result. They do not define language semantics.

## Ownership (Single Source Per Concern)

Exactly one subsystem owns each concern:

- Parsing and AST shape:
  - Owner: `parse/*`
  - Must not perform semantic decisions beyond syntax validity.
  - Annotation syntax disambiguation must be context-aware:
    - A `[[...]]` token sequence is treated as annotations only when it is a complete annotation block and is followed by a syntactically valid annotation target for that parse context.
    - Otherwise the same token sequence must remain available to normal expression parsing (for example nested array literals like `[[input(), 2], [3, 4]]`).
- Annotation semantics:
  - Owner: backend-specific code.
  - Frontend must parse and preserve annotations but must not enforce a global annotation whitelist.
  - Unknown annotations must remain available to the selected backend unchanged.
- Name/module binding and symbol identity:
  - Owner: `resolve/*`
  - Must not duplicate compile-time evaluation logic.
- Type rules and type inference:
  - Owner: `type/*`
  - May ask for compile-time facts, but must not implement a second evaluator.
- Compile-time execution engine:
  - Owner: `transform/evaluator.*`
  - Single implementation for static value evaluation.
- Compile-time value data model (`CTValue`, `CTEQueryResult`):
  - Owner: `core/cte_value.h`
  - Public headers may depend on this model, but must not leak evaluator engine ownership.
- Compile-time facts collection:
  - Owner: `transform/optimizer.*`
  - Records constexpr values/conditions using the single evaluator.
- AST residualization from compile-time facts:
  - Owner: `transform/residualizer.*`
  - Rewrites AST; does not invent new semantics.
- Reachability, effects, mutability, reentrancy, usage:
  - Owner: `analysis/*`
  - Computes whole-program graph facts after residualization.
- Final frontend DCE prune:
  - Owner: `pipeline/frontend_pipeline.*`
  - Drops unreachable/unneeded top-level declarations from frontend output.

## Non-Negotiable Decisions

1. Single evaluator
- There must be exactly one compile-time evaluator implementation.
- Resolver/typechecker must not contain independent static-condition evaluators.

2. No semantic fallbacks
- No placeholder values to "keep going" (for example fake `0` field values).
- No implicit semantic bridges that hide missing analysis.
- If compile-time behavior is unknown, keep runtime expression or emit a clear error.

3. CTE and DCE granularity
- CTE and DCE must operate at sub-statement/sub-expression granularity.
- Expression trees are analyzed per node, not only per function symbol.
- DCE is not only "drop unreachable function"; it includes dead expression/statement pruning.

4. Frontend-owned liveness
- Backends must not be responsible for language-level liveness decisions.
- Reachability/usage pruning must be complete before backend emission.

5. Frontend-owned CTE semantics
- Backends consume frontend `constexpr` facts and residualized AST only.
- Backends must not instantiate compile-time evaluators or semantic fallback evaluators.

6. Whole-program semantics
- Analysis assumes all source is available.
- Cross-module graph decisions are frontend responsibilities.

## Pipeline Contract

The frontend pipeline must preserve this order of responsibilities:

1. Resolve names and symbols.
2. Type-check with no duplicate evaluator semantics.
3. Lower AST shape (without changing semantic meaning).
4. Compute compile-time facts using the single evaluator.
5. Residualize AST from those facts.
6. Run whole-program analyses on residualized program.
7. Validate type usage on reachable/used value paths only.
8. Prune to live declarations before backend emission.

Any pass that mutates AST must keep bindings/facts coherent for downstream passes.

## Correctness Invariants

- A value use in reachable runtime code must have a concrete type.
- Unused return chains may remain unresolved if no reachable value use depends on them.
- Compile-time-dead branches must not create type-use failures.
- Used named types must be retained even if only referenced by local declarations.
- Monomorphized instantiations required by reachable runtime calls must survive to backend input.

### Fixpoint and Unknown-State Monotonicity

- Frontend semantic solving is iterative/fixpoint-based, not a single-pass assumption.
- Compile-time fact solving is dependency-driven:
  - `optimizer` owns a worklist of context roots.
  - Root evaluation emits sub-expression facts observed during root execution.
  - Unresolved nodes may use a targeted fallback query queue; eager full-expression rescans are forbidden.
  - `evaluator` trace hooks (`value` and `symbol-read`) feed dependency edges.
  - When known compile-time symbol values change, only dependent work is re-queued.
- Each iteration must be monotonic for knowledge:
  - `Unknown` may become `Known` or `Error`.
  - `Known` must never become `Unknown`.
- Dependency loops must be detected explicitly (for example SCCs in dependency graphs).
- An SCC with no external seed knowledge remains unresolved and must produce a deterministic diagnostic.
- Unknown states must not be converted into implicit defaults/bridges just to pass a later stage.

## Review Rejects

Changes should be rejected if they introduce any of these:

- New static evaluators outside `transform/evaluator.*`.
- Public headers that expose evaluator engine headers instead of `core/cte_value.h`.
- Silent placeholder values or hidden default semantic behavior.
- Backend-side fixes for frontend liveness/type-analysis bugs.
- Pass-local hacks that bypass pipeline contracts instead of fixing ownership.
- "Temporary bridge" logic without a hard failure mode and tests.

## Test Expectations

Every semantic change must add regression coverage for:

- CTE correctness (no unsound fold of runtime-dependent paths)
- DCE correctness (symbol and statement/sub-expression levels)
- Type-use correctness on reachable paths
- Cross-module and generic-instantiation survival to backend emission

Test-update policy:

- Bulk expected-report refreshes are forbidden.
- There must be no fixture/target/script that rewrites many `expected_report.md` files at once.
- Expected outputs must be updated only per-test, with root-cause explanation in the commit.

## Recurring Failure Patterns

Recurring failure modes seen in this codebase:

- Ownership drift:
  - Same semantic decision implemented in multiple subsystems.
  - Result: divergence and hidden behavior differences.
- Fail-open contracts:
  - Unknown/invalid input is normalized or silently accepted.
  - Result: configuration/user errors become runtime surprises.
- Placeholder bridges:
  - Temporary fallback paths remain in production semantics.
  - Result: architecture complexity and unsound edge behavior.
- Incomplete migrations:
  - Stub paths are left active while new path is partially rolled out.
  - Result: correctness relies on incidental downstream behavior.
- Test objective inversion:
  - Characterization tests are used where desired-behavior regression tests are required.
  - Result: green test suite while known defects remain unresolved.

## Change Strategy (Contract-Closed Slices)

"Small, self-consistent steps" is not sufficient by itself.  
Correct rule: use contract-closed slices.

A change slice is acceptable only when all of the following hold:

- Ownership closure:
  - The changed semantic concern has one active owner after the change.
  - No duplicated semantic engines remain for the same concern.
- Contract closure:
  - Unknown/invalid cases fail explicitly (diagnostic/error), not normalize/silently pass.
  - No semantic fallback/bridge is introduced to preserve temporary behavior.
- Migration closure:
  - If migration is incomplete, the unfinished path is gated by hard failure, not by permissive fallback.
- Test closure:
  - Tests assert desired behavior.
  - For unresolved defects, tests are red-first and stay red until fixed.
- Context closure:
  - Do not edit partially-understood ownership paths.
  - Read touched owner files end-to-end before modifying them.

## Review Gate

Reject a change if any item below is true:

- Introduces a second semantic owner for existing behavior.
- Adds normalization/defaulting where an error should be emitted.
- Keeps or adds a placeholder semantic path without hard failure.
- Converts a defect test into a characterization test without explicit approval.
- Updates expected outputs broadly instead of per-test root-cause updates.
