# Wall Of Shame

Add-only log of architectural shortcuts and mishaps detected in audits.

## Rules

- This file is append-only. Do not delete or rewrite old entries.
- Every issue entry must include:
  - `Introduced`: commit hash where the issue was introduced.
  - `Resolved`: commit hash where it was fixed, or `UNRESOLVED`.
  - concise impact statement.
  - evidence (tests, failure mode, or audit note).
- If the intro commit cannot be identified immediately, use `UNKNOWN`.
- When an unresolved issue is fixed, append a new `Resolution` entry; do not edit prior issue text.

## Entry Format

Issue entry:

- `ID`: `WS-###`
- `Type`: `Issue`
- `Status`: `UNRESOLVED` or `RESOLVED`
- `Introduced`: `<commit>`
- `Resolved`: `<commit|UNRESOLVED>`
- `Summary`: `<one-line architectural shortcut/mishap>`
- `Impact`: `<concise user-visible or architecture-visible effect>`
- `Evidence`: `<tests / error / audit reference>`

Resolution entry:

- `ID`: `WS-###`
- `Type`: `Resolution`
- `Resolved`: `<commit>`
- `Notes`: `<what changed>`

## Log

- `ID`: `WS-001`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `322990d4`
- `Resolved`: `UNRESOLVED`
- `Summary`: Global array/range initializer classification uses a hardcoded frontend shortcut instead of evaluator truth.
- `Impact`: Runtime-dependent global initializers can be misclassified as compile-time, dropping runtime call edges and mutability accuracy.
- `Evidence`: `frontend/tests/architecture/AR-008/runtime_array_initializer_retains_call_edges` fails; backend path reports missing callee variants.

- `ID`: `WS-002`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `abc66b54`
- `Resolved`: `UNRESOLVED`
- `Summary`: Frontend prunes top-level declarations after type-use validation without post-prune integrity validation.
- `Impact`: Lowered module can contain references to declarations removed by prune, causing backend-phase undefined-function failures.
- `Evidence`: `frontend/tests/architecture/AR-009/post_prune_lowered_module_is_backend_consistent` fails with `Undefined function: helper`.

- `ID`: `WS-003`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `bba7dfb1`
- `Resolved`: `UNRESOLVED`
- `Summary`: Compile-time block execution keeps dual engines (`eval_block_vm` + `eval_block_fallback`).
- `Impact`: Duplicate semantics path increases divergence risk and audit surface for CTE behavior.
- `Evidence`: Audit finding; implementation split visible in `frontend/src/transform/evaluator.cpp`.

- `ID`: `WS-004`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `f5a0c2a8`
- `Resolved`: `UNRESOLVED`
- `Summary`: Generic type substitution path is partially stubbed (`type_map` not effectively applied for real type-variable substitution).
- `Impact`: Monomorphization correctness relies on incidental behavior instead of a complete substitution contract.
- `Evidence`: Audit finding in `frontend/src/type/typechecker_generics.cpp`.

- `ID`: `WS-005`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `c56824d`
- `Resolved`: `UNRESOLVED`
- `Summary`: Evaluator purity analyzer (`is_pure_for_compile_time` family) remains in code while not being a live decision gate for CTE.
- `Impact`: Increases frontend complexity and ownership ambiguity without enforcing semantics.
- `Evidence`: Audit finding; symbol is present but not a primary gate in current compile-time call evaluation flow.

- `ID`: `WS-006`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `UNRESOLVED`
- `Summary`: Unified driver accepts arbitrary `--backend-opt key=value` pairs without backend key validation for backends that declare no options.
- `Impact`: Invalid backend options can be silently accepted, weakening CLI contract safety and hiding user mistakes.
- `Evidence`: `frontend/tests/architecture/AR-010/backend_opt_unknown_key_silently_accepted`.

- `ID`: `WS-007`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `UNRESOLVED`
- `Summary`: Reentrancy context normalization silently maps invalid boundary defaults to `N`.
- `Impact`: Backend contract mistakes can degrade behavior without a hard diagnostic.
- `Evidence`: `frontend/tests/architecture/AR-011/reentrancy_invalid_default_silently_normalized`.

- `ID`: `WS-008`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `UNRESOLVED`
- `Summary`: Compile-time block evaluation still routes through dual semantic engines (`eval_block_vm` and `eval_block_fallback`).
- `Impact`: Duplicate execution semantics increase drift risk and maintenance burden for CTE ownership.
- `Evidence`: `frontend/tests/architecture/AR-012/evaluator_block_dual_engines_present`.

- `ID`: `WS-009`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `UNRESOLVED`
- `Summary`: Compile-time coercion for unresolved named types keeps fields via a placeholder fallback.
- `Impact`: Type coercion can proceed without full type resolution guarantees, weakening semantic soundness.
- `Evidence`: `frontend/tests/architecture/AR-013/evaluator_named_type_coercion_fallback_present`.

- `ID`: `WS-010`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `UNRESOLVED`
- `Summary`: Generic substitution still contains a simplified/stubbed path with effectively empty type-map substitution.
- `Impact`: Monomorphization correctness depends on incidental downstream behavior instead of a complete substitution contract.
- `Evidence`: `frontend/tests/architecture/AR-014/generic_substitution_stub_present`.

- `ID`: `WS-011`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `UNRESOLVED`
- `Summary`: Native TCC execution path duplicates frontend pipeline orchestration instead of reusing one canonical compile path.
- `Impact`: Pipeline drift risk between normal emission mode and native-run mode.
- `Evidence`: `frontend/tests/architecture/AR-015/native_tcc_pipeline_duplication_present`.

- `ID`: `WS-012`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `UNRESOLVED`
- `Summary`: Frontend annotation validator is a no-op, so unknown/typo annotations are silently accepted.
- `Impact`: Annotation spelling mistakes reach backends unchecked and can silently alter intended behavior.
- `Evidence`: `frontend/tests/architecture/AR-016/unknown_annotation_is_silently_accepted`.

- `ID`: `WS-013`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `cc10b71`
- `Resolved`: `UNRESOLVED`
- `Summary`: New architecture tests (`AR-010..AR-016`) were written as characterization checks of broken behavior instead of failing tests for desired behavior.
- `Impact`: Provides false confidence and blocks TDD flow because unresolved architectural defects still produce green tests.
- `Evidence`: `frontend/tests/architecture/AR-010` through `frontend/tests/architecture/AR-016` pass while corresponding issues remain unresolved.

- `ID`: `WS-013`
- `Type`: `Resolution`
- `Resolved`: `ddc8f8d`
- `Notes`: Flipped `AR-010..AR-016` from characterization checks to desired-behavior assertions so they fail until architectural issues are fixed.
