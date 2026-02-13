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
- `Status`: `RESOLVED`
- `Introduced`: `322990d4`
- `Resolved`: `891eea1`
- `Summary`: Global array/range initializer classification uses a hardcoded frontend shortcut instead of evaluator truth.
- `Impact`: Runtime-dependent global initializers can be misclassified as compile-time, dropping runtime call edges and mutability accuracy.
- `Evidence`: `frontend/tests/architecture/AR-008/runtime_array_initializer_retains_call_edges` fails; backend path reports missing callee variants.

- `ID`: `WS-002`
- `Type`: `Issue`
- `Status`: `RESOLVED`
- `Introduced`: `abc66b54`
- `Resolved`: `891eea1`
- `Summary`: Frontend prunes top-level declarations after type-use validation without post-prune integrity validation.
- `Impact`: Lowered module can contain references to declarations removed by prune, causing backend-phase undefined-function failures.
- `Evidence`: `frontend/tests/architecture/AR-009/post_prune_lowered_module_is_backend_consistent` fails with `Undefined function: helper`.

- `ID`: `WS-003`
- `Type`: `Issue`
- `Status`: `RESOLVED`
- `Introduced`: `bba7dfb1`
- `Resolved`: `891eea1`
- `Summary`: Compile-time block execution keeps dual engines (`eval_block_vm` + `eval_block_fallback`).
- `Impact`: Duplicate semantics path increases divergence risk and audit surface for CTE behavior.
- `Evidence`: Audit finding; implementation split visible in `frontend/src/transform/evaluator.cpp`.

- `ID`: `WS-004`
- `Type`: `Issue`
- `Status`: `RESOLVED`
- `Introduced`: `f5a0c2a8`
- `Resolved`: `891eea1`
- `Summary`: Generic type substitution path is partially stubbed (`type_map` not effectively applied for real type-variable substitution).
- `Impact`: Monomorphization correctness relies on incidental behavior instead of a complete substitution contract.
- `Evidence`: Audit finding in `frontend/src/type/typechecker_generics.cpp`.

- `ID`: `WS-005`
- `Type`: `Issue`
- `Status`: `RESOLVED`
- `Introduced`: `c56824d`
- `Resolved`: `891eea1`
- `Summary`: Evaluator purity analyzer (`is_pure_for_compile_time` family) remains in code while not being a live decision gate for CTE.
- `Impact`: Increases frontend complexity and ownership ambiguity without enforcing semantics.
- `Evidence`: Audit finding; symbol is present but not a primary gate in current compile-time call evaluation flow.

- `ID`: `WS-006`
- `Type`: `Issue`
- `Status`: `RESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `891eea1`
- `Summary`: Unified driver accepts arbitrary `--backend-opt key=value` pairs without backend key validation for backends that declare no options.
- `Impact`: Invalid backend options can be silently accepted, weakening CLI contract safety and hiding user mistakes.
- `Evidence`: `frontend/tests/architecture/AR-010/backend_opt_unknown_key_silently_accepted`.

- `ID`: `WS-007`
- `Type`: `Issue`
- `Status`: `RESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `891eea1`
- `Summary`: Reentrancy context normalization silently maps invalid boundary defaults to `N`.
- `Impact`: Backend contract mistakes can degrade behavior without a hard diagnostic.
- `Evidence`: `frontend/tests/architecture/AR-011/reentrancy_invalid_default_silently_normalized`.

- `ID`: `WS-008`
- `Type`: `Issue`
- `Status`: `RESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `891eea1`
- `Summary`: Compile-time block evaluation still routes through dual semantic engines (`eval_block_vm` and `eval_block_fallback`).
- `Impact`: Duplicate execution semantics increase drift risk and maintenance burden for CTE ownership.
- `Evidence`: `frontend/tests/architecture/AR-012/evaluator_block_dual_engines_present`.

- `ID`: `WS-009`
- `Type`: `Issue`
- `Status`: `RESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `891eea1`
- `Summary`: Compile-time coercion for unresolved named types keeps fields via a placeholder fallback.
- `Impact`: Type coercion can proceed without full type resolution guarantees, weakening semantic soundness.
- `Evidence`: `frontend/tests/architecture/AR-013/evaluator_named_type_coercion_fallback_present`.

- `ID`: `WS-010`
- `Type`: `Issue`
- `Status`: `RESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `891eea1`
- `Summary`: Generic substitution still contains a simplified/stubbed path with effectively empty type-map substitution.
- `Impact`: Monomorphization correctness depends on incidental downstream behavior instead of a complete substitution contract.
- `Evidence`: `frontend/tests/architecture/AR-014/generic_substitution_stub_present`.

- `ID`: `WS-011`
- `Type`: `Issue`
- `Status`: `RESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `891eea1`
- `Summary`: Native TCC execution path duplicates frontend pipeline orchestration instead of reusing one canonical compile path.
- `Impact`: Pipeline drift risk between normal emission mode and native-run mode.
- `Evidence`: `frontend/tests/architecture/AR-015/native_tcc_pipeline_duplication_present`.

- `ID`: `WS-012`
- `Type`: `Issue`
- `Status`: `RESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `891eea1`
- `Summary`: Frontend annotation validator is a no-op, so unknown/typo annotations are silently accepted.
- `Impact`: Annotation spelling mistakes reach backends unchecked and can silently alter intended behavior.
- `Evidence`: `frontend/tests/architecture/AR-016/unknown_annotation_is_silently_accepted`.

- `ID`: `WS-013`
- `Type`: `Issue`
- `Status`: `RESOLVED`
- `Introduced`: `cc10b71`
- `Resolved`: `ddc8f8d`
- `Summary`: New architecture tests (`AR-010..AR-016`) were written as characterization checks of broken behavior instead of failing tests for desired behavior.
- `Impact`: Provides false confidence and blocks TDD flow because unresolved architectural defects still produce green tests.
- `Evidence`: `frontend/tests/architecture/AR-010` through `frontend/tests/architecture/AR-016` pass while corresponding issues remain unresolved.

- `ID`: `WS-013`
- `Type`: `Resolution`
- `Resolved`: `ddc8f8d`
- `Notes`: Flipped `AR-010..AR-016` from characterization checks to desired-behavior assertions so they fail until architectural issues are fixed.

- `ID`: `WS-001`
- `Type`: `Resolution`
- `Resolved`: `891eea1`
- `Notes`: Removed hardcoded runtime-classification shortcuts for global initializers and delegated to evaluator truth.

- `ID`: `WS-002`
- `Type`: `Resolution`
- `Resolved`: `891eea1`
- `Notes`: Added prune-linkage validation and stabilized frontend DCE pruning so kept roots cannot reference dropped top-level functions.

- `ID`: `WS-003`
- `Type`: `Resolution`
- `Resolved`: `891eea1`
- `Notes`: Removed block-evaluator fallback path and kept one VM-based block execution engine.

- `ID`: `WS-004`
- `Type`: `Resolution`
- `Resolved`: `891eea1`
- `Notes`: Implemented real generic substitution map collection/application across function signatures and bodies.

- `ID`: `WS-005`
- `Type`: `Resolution`
- `Resolved`: `891eea1`
- `Notes`: Removed dead purity-analyzer helper path from evaluator to reduce ownership ambiguity in CTE.

- `ID`: `WS-006`
- `Type`: `Resolution`
- `Resolved`: `891eea1`
- `Notes`: Added per-backend option validation hook and enforced unknown-option rejection for backends without option support.

- `ID`: `WS-007`
- `Type`: `Resolution`
- `Resolved`: `891eea1`
- `Notes`: Removed silent reentrancy normalization fallback and now fail-fast on invalid default context keys.

- `ID`: `WS-008`
- `Type`: `Resolution`
- `Resolved`: `891eea1`
- `Notes`: Consolidated block compile-time execution to the VM path and removed duplicated semantics engine.

- `ID`: `WS-009`
- `Type`: `Resolution`
- `Resolved`: `891eea1`
- `Notes`: Replaced unresolved named-type coercion fallback with strict behavior plus explicit internal tuple coercion handling.

- `ID`: `WS-010`
- `Type`: `Resolution`
- `Resolved`: `891eea1`
- `Notes`: Completed generic substitution implementation and removed empty-map/stub behavior from monomorphization path.

- `ID`: `WS-011`
- `Type`: `Resolution`
- `Resolved`: `891eea1`
- `Notes`: Moved translation-unit emission orchestration into compiler path and removed duplicate native-TCC pipeline logic.

- `ID`: `WS-012`
- `Type`: `Resolution`
- `Resolved`: `891eea1`
- `Notes`: Implemented frontend annotation validation and hard errors for unknown annotations.

- `ID`: `WS-014`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `UNRESOLVED`
- `Summary`: Frontend optimization facts are keyed by raw AST pointers without module-instance identity.
- `Impact`: Compile-time facts can bleed across module instances that share AST nodes, causing unsound CTE/branch pruning/DCE decisions.
- `Evidence`: `frontend/tests/architecture/AR-017/optimizer_facts_must_be_instance_aware`.

- `ID`: `WS-015`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `UNRESOLVED`
- `Summary`: Lowerer performs constexpr branch pruning instead of staying shape-only.
- `Impact`: Semantic ownership drifts from optimizer/residualizer, and branch decisions can use wrong instance context.
- `Evidence`: `frontend/tests/architecture/AR-018/lowerer_must_not_perform_constexpr_pruning`.

- `ID`: `WS-016`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `UNRESOLVED`
- `Summary`: Compiler frontend orchestration is duplicated across `compile()` and `emit_translation_unit()`.
- `Impact`: Pipeline drift risk between normal emission and translation-unit code paths.
- `Evidence`: `frontend/tests/architecture/AR-019/compiler_pipeline_single_owner`.

- `ID`: `WS-017`
- `Type`: `Issue`
- `Status`: `UNRESOLVED`
- `Introduced`: `UNKNOWN`
- `Resolved`: `UNRESOLVED`
- `Summary`: Frontend->backend `constexpr_condition` contract omits module-instance identity.
- `Impact`: Backends cannot safely query branch facts for multi-instance programs and may validate/emit against the wrong instance context.
- `Evidence`: `frontend/tests/architecture/AR-020/analyzed_program_constexpr_condition_must_be_instance_aware`.

- `ID`: `WS-014`
- `Type`: `Resolution`
- `Resolved`: `7664739`
- `Notes`: Replaced raw-pointer optimization fact keys with `(instance_id,node)` keys and updated frontend/backends to query facts with instance context.

- `ID`: `WS-015`
- `Type`: `Resolution`
- `Resolved`: `7664739`
- `Notes`: Removed constexpr branch pruning from `transform/lowerer.*`; branch folding ownership is now only optimizer/residualizer.

- `ID`: `WS-016`
- `Type`: `Resolution`
- `Resolved`: `7664739`
- `Notes`: Consolidated frontend orchestration into one canonical preparation path reused by both `compile()` and `emit_translation_unit()`.

- `ID`: `WS-017`
- `Type`: `Resolution`
- `Resolved`: `7664739`
- `Notes`: Extended `AnalyzedProgram::constexpr_condition` to include `instance_id` and updated backend call sites.
