# 9. Frontend/Backend Contract

RFC references: [Compilation model](../vexel-rfc.md#preamble), [Annotations & Lowered Form](../vexel-rfc.md#annotations--lowered-form)

Operational contract references:

- `frontend/src/architecture.md`
- `frontend/src/cli/compiler.cpp`
- `frontend/src/transform/lowerer.h`
- `frontend/src/support/backend_registry.h`

## 9.1 Contract Object

The backend input contract is the analyzed/lowered program model produced by the frontend pipeline (`AnalyzedProgram` family + lowered AST structures).

Backends are consumers of this contract, not co-owners of language semantics.

## 9.2 Frontend Responsibilities Before Backend Emit

Frontend must complete:

- name resolution and symbol identity assignment,
- type checking and monomorphization,
- compile-time fact collection,
- residualization and graph analyses,
- concrete type-use validation for live paths,
- final live declaration pruning.

## 9.3 Backend Responsibilities

Backends must:

- generate target output from analyzed/lowered contract,
- apply backend-specific ABI and code-layout decisions,
- validate backend-only annotations/options,
- reject unsupported target forms explicitly.

Backends must not:

- instantiate alternate semantic evaluators,
- redefine CTE/DCE language behavior,
- perform language-level fallback semantics for missing frontend analysis.

## 9.4 Annotation Contract

Frontend stores annotation payloads without imposing backend whitelist semantics.

Selected backend interprets supported annotation names/arguments.

Unknown annotation handling is backend-defined unless frontend parsing itself is invalid.

## 9.5 Backend Registration Contract

Backend plugin API requires:

- registration function,
- backend metadata (`name`, `description`, `version`),
- emit callback.

Optional hooks:

- option parsing,
- usage/help emission.

## 9.6 Divergent Backend Strategy Is Expected

Backends may intentionally diverge in:

- emitted C structure,
- function layout and inlining policy,
- ABI strategy and target constraints,
- reentrancy/nonreentrancy lowering strategy.

This divergence is valid if frontend contract semantics are preserved.

## 9.7 Testing Boundary

Frontend tests assert language semantics and ownership invariants.

Backend tests assert codegen/ABI behavior against frontend-provided facts.

Conformance harness validates backend registration and basic integration behavior.
