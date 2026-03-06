# 10. Conformance and Release Gates

RFC references: [Errors & Diagnostics](../vexel-rfc.md#errors--diagnostics)

## 10.1 Test Layers

### Frontend suite

Location: `frontend/tests`

Covers:

- lexer/parser/grammar,
- type inference and type checking,
- module/resource resolution,
- compile-time execution and residualization,
- architecture ownership invariants,
- performance guard tests.

### Backend suites

Locations:

- `backends/c/tests`
- `backends/ext/<name>/tests` (when present)

Covers backend-specific codegen/ABI behavior.

### Backend conformance

Location: `backends/conformance_test.sh`

Ensures backend registration/build/test wiring contract remains valid.

## 10.2 Required Commands

Core gates:

- `make test`
- `make frontend-perf-test`
- `make docs-check`
- `make ci` (aggregate target)

## 10.3 Documentation Gates

- `docs/vexel-rfc.md` remains normative and versioned.
- `docs/spec/*` must remain aligned to RFC.
- docs landing (`docs/index.html`) and playground (`docs/playground.html`) separation must pass `make docs-check`.

## 10.4 Change Discipline

For language-semantic changes:

1. update RFC first (or in same change),
2. update corresponding detailed spec chapters,
3. add/adjust focused tests,
4. run full gates.

Bulk golden-output rewrites without root-cause justification are not allowed.

## 10.5 Release Candidate Rule (v1.0-rc1)

Under `v1.0-rc1`:

- syntax/semantics are feature-frozen,
- only clarifications and bug-fix semantic corrections are allowed,
- any incompatible language change requires explicit RFC amendment.

## 10.6 CI Scope

Current CI should at least enforce frontend suite + performance guards on push/PR.

Additional backend matrix expansion is optional and can be staged independently.
