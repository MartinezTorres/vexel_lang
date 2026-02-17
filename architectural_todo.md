# Architectural TODO

## Workflow Contract

For every step below, follow this exact sequence:

1. Root cause analysis
2. Plan
3. Execution
4. Test
5. Commit
6. Review this `architectural_todo.md` list and update next priorities

No step is considered complete unless all six sub-steps are done.

## Current Baseline

- Full matrix is green: `make test` passes (frontend, backend conformance, C backend, megalinker backend, vexel backend).
- Remaining issues are mostly architecture/ownership cleanup and duplication reduction.

## Prioritized Steps

### 1. Remove redundant optimizer rerun in frontend pipeline

- Status: [x] Done
- Root problem:
  - The pipeline runs optimizer in fixpoint and then runs optimizer again once more on unchanged state.
  - This adds cost and complexity without semantic gain.
- Target files:
  - `frontend/src/pipeline/frontend_pipeline.cpp`
- Done criteria:
  - Single canonical optimize/residualize flow.
  - No behavior drift in frontend output and tests.

### 2. Make compile-time-init classification frontend-owned and singular

- Status: [ ] Open
- Root problem:
  - Compile-time init decisions are split across frontend typing and backend shortcuts.
  - Array/range special-casing appears outside optimizer facts.
- Target files:
  - `frontend/src/type/typechecker.cpp`
  - `backends/c/src/codegen.cpp`
  - `backends/ext/megalinker/src/codegen.cpp`
- Done criteria:
  - One source of truth: optimizer facts.
  - Backends consume facts only, without semantic shortcuts.

### 3. Deduplicate declaration-assignment compatibility logic

- Status: [ ] Open
- Root problem:
  - Similar type-compatibility logic is duplicated in var-decl and declaration-assignment paths.
  - Drift risk for array literals, casts, and literal assignability.
- Target files:
  - `frontend/src/type/typechecker.cpp`
  - `frontend/src/type/typechecker_expr_control.cpp`
  - `frontend/src/type/typechecker.h`
- Done criteria:
  - Shared helper for declared-type initialization compatibility.
  - Both paths call same helper and keep identical behavior.

### 4. Consolidate scalar-to-bool semantics

- Status: [ ] Open
- Root problem:
  - Scalar-to-bool conversion logic is implemented in multiple places.
  - Behavior drift risk between optimizer, residualizer, analyzed program builder, evaluator, and backends.
- Target files:
  - `frontend/src/transform/optimizer.cpp`
  - `frontend/src/transform/residualizer.cpp`
  - `frontend/src/pipeline/analyzed_program_builder.cpp`
  - `frontend/src/transform/evaluator.cpp`
  - `backends/c/src/codegen.cpp`
  - `backends/ext/megalinker/src/codegen.cpp`
- Done criteria:
  - One canonical frontend conversion helper.
  - Backend conversions are thin wrappers only when emission-format-specific.

### 5. Split evaluator monolith while keeping single-owner semantics

- Status: [ ] Open
- Root problem:
  - `frontend/src/transform/evaluator.cpp` is still a large complexity hotspot.
  - Large functions (`eval_call`, `eval_binary`, `eval_cast`, `eval_assignment`) are hard to audit and evolve.
- Target files:
  - `frontend/src/transform/evaluator.cpp`
  - `frontend/src/transform/evaluator.h`
  - new files under `frontend/src/transform/` (expression-family split)
- Done criteria:
  - Same evaluator owner and behavior.
  - Smaller translation units with clear responsibility boundaries.
  - No fallback/bridge paths introduced.

### 6. Remove intra-megalinker duplication first

- Status: [ ] Open
- Root problem:
  - Megalinker duplicates helper logic both in backend orchestration and codegen units.
  - Same lvalue/mutability/ref-variant helpers appear in more than one place.
- Target files:
  - `backends/ext/megalinker/src/megalinker_backend.cpp`
  - `backends/ext/megalinker/src/codegen.cpp`
  - `backends/ext/megalinker/src/codegen.h`
- Done criteria:
  - No repeated helper implementations inside megalinker backend.
  - Ownership of helper logic is explicit and local to the backend.

### 7. Add backend contract guards (not output-parity guards)

- Status: [ ] Open
- Root problem:
  - Backend semantic contracts must stay correct while C and megalinker evolve independently.
  - Output drift between backends is intentional and should not be treated as a regression by itself.
- Target files:
  - `backends/c/tests/...`
  - `backends/ext/megalinker/tests/...`
  - `backends/conformance_test.sh` (if needed)
- Done criteria:
  - Tests enforce frontend->backend contract invariants (binding/type/fact usage), not textual C parity.
  - Backend-specific output and architecture decisions remain independent by design.

### 8. Frontend hygiene cleanup (naming and dead parameters)

- Status: [ ] Open
- Root problem:
  - Residual naming still uses "fallback" for strict/default behavior in reentrancy analysis.
  - Some signatures carry unused parameters.
- Target files:
  - `frontend/src/analysis/analysis_reentrancy.cpp`
  - `frontend/src/analysis/analysis.cpp`
- Done criteria:
  - Naming matches real behavior (default/context, not fallback semantics).
  - Unused params removed or explicitly justified.

## Notes For Execution Discipline

- Keep changes contract-closed, not size-closed.
- Do not introduce temporary semantic bridges to preserve behavior.
- Keep C and megalinker fully independent:
  - Do not couple their codegen design.
  - Do not enforce textual/generated-C parity between them.
  - Drift is intentional when driven by backend goals.
- If new architectural debt is discovered while working a step:
  - add it to `WallOfShame.md`,
  - add a red-first test,
  - commit current state,
  - then continue.
