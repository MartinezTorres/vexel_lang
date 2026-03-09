# Architecture — Open the Machine

**Role**: Architecture (compiler ownership, contracts, and invariants).
Web route page: `docs/architecture.html`.

Vexel is organized around a strict ownership model:

- Frontend owns semantics and analysis.
- Backends own emission strategy.
- The frontend/backend boundary is `AnalyzedProgram`.

## Frontend Ownership

Frontend responsibilities include:

- parse and resolve modules,
- type-check and monomorphize,
- lower to analyzed form,
- compile-time execution (CTE),
- dead code elimination (DCE),
- effect/reentrancy/mutability and reference-variant analysis,
- concrete type-use validation before backend emission.

Backends must not own semantic evaluation rules.

## Canonical Pipeline

Canonical stage order is implemented in `frontend/src/cli/compiler.cpp`:

1. Module load
2. Name/scope resolution
3. Type checking
4. Monomorphization
5. Lowering
6. Optimization-fact collection
7. Analysis passes
8. Type-use validation
9. Live declaration pruning
10. Backend emission

## Backend Contract

Backends consume an analyzed contract and generate target output.
They may differ significantly in implementation strategy while preserving behavior.

Contract references:

- [Specification chapter 09 — Frontend/Backend Contract](spec/09-backend-contract.md)
- [Specification chapter 07 — ABI Boundary](spec/07-abi-boundary.md)
- `frontend/src/support/backend_registry.h`

## Residualization / Emission Concept

Frontend passes reduce language semantics into a residual runtime contract:

1. Fold what can be proven compile-time.
2. Remove dead symbols and unreachable declarations.
3. Emit analyzed residual behavior only.
4. Hand this contract to backends for target-specific realization.

## Invariants and Safety Gates

Compiler invariants and ownership checks are enforced by tests and pass guards.

Primary gates:

- `make test`
- `make ci`
- `make frontend-test`
- `make backend-conformance-test`
- `make docs-check`

Invariant sources:

- `frontend/src/pipeline/pass_invariants.h`
- `frontend/src/pipeline/pass_invariants.cpp`
- architecture-focused tests under `frontend/tests/architecture/`

## Extension Points

Backend discovery locations:

- `backends/<name>/`
- `backends/ext/<name>/`

Unified driver:

- lists registered backends,
- requires explicit backend selection (`-b <name>`),
- forwards unknown options to the selected backend.

## Related Documents

- [Landing Page](index.html) — thesis and route selector.
- [RFC](vexel-rfc.md) — normative language law.
- [Specification](spec/index.md) — full semantics.
- [Tutorial](tutorial.md) — practical route.
- [Anti-goals](anti-goals.md) — explicit refusals.
