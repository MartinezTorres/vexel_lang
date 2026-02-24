# TODO (experiment notes + root-cause analysis / plan)

Notes below are preserved from experimentation and annotated with:
- `RCA`: root-cause analysis (current behavior, not a fix)
- `Plan`: recommended implementation path
- `Questions`: decisions needed before implementation

Recommended execution order (to avoid rework):
1. Decide semantics/policy for empty-body functions and strictness defaults.
2. Fix parser ambiguity around top-level semicolon omission vs plain `&` function declarations.
3. Define arbitrary-integer + arbitrary-literal semantics in frontend (single source of truth).
4. Rework frontend CTE numeric representation/performance around that decision.
5. Extend C and megalinker backends for arbitrary-width integer lowering.
6. Revisit playground UX/runtime features (console/run) after runtime/CTE capabilities are clear.
7. Megalinker codegen cleanup (temps/reentrancy/SDCC-oriented output shaping).

## Decisions locked from discussion

- **No implicit numeric-zero for empty blocks/functions.**
  - A function with no `->` and no final expression returns **nothing** (non-value).
  - This is *not* equivalent to `0`.
  - Calls to such functions are statement-only (cannot be used as values).
- **Backend mapping for “returns nothing”.**
  - C/megalinker should emit C `void` for functions that return nothing.
  - No Vexel `#void` type is introduced for now.
- **Type strictness default remains `0`.**
- **Arbitrary integers / literals feature is single-pass only.**
  - Frontend literals + type inference + CTE + both backends must be implemented coherently in one change set (no staged partial semantics).
- **Playground runtime goal (if done) is full Vexel execution in-browser** (not just a host-stub toy).
- **Megalinker optimization strategy remains open.**
  - Requires dedicated study; likely multiple strategies and flags later.
- **Compound assignment operators should be studied and designed as one coherent feature** (not piecemeal).

## Clarifications after discussion

- The `&main(){}` parsing issue is not a semantic ambiguity (“two valid programs”); it is a **parser boundary ambiguity** caused by semicolon-optional top-level syntax plus plain `&` overlapping with the bitwise-and operator prefix during incremental parse decisions.
- “Limb helper” (term used in backend notes) means a backend-owned arbitrary-width integer implementation using arrays of machine words/bytes plus helper functions for arithmetic (`add/sub/mul/div/shifts/compare/mask`), instead of relying on native C integer types only.

## Execution protocol (mandatory for each step)

For each step below:
1. Re-read this file and restate/verify the step RCA.
2. Write/refresh the step plan here, including **exact files to modify and why**.
3. Implement the change (no bridge/hack compatibility layers).
4. Run targeted tests + regression tests.
5. Update docs/RFC/help/examples/tutorial/playground if behavior changes.
6. Audit for consistency (examples/tutorial/RFC/docs/help/tests/backends).
7. Commit.
8. Mark progress here and move to next step (do not leave unresolved partial semantics).

If a new architectural issue is discovered while implementing a step:
- annotate it here immediately (with RCA + plan),
- add regression coverage if possible,
- finish the current step coherently (or split and commit if the new issue blocks correctness).

## Step breakdown and file impact plan (initial)

### Step 1 — “returns nothing” semantics (no implicit zero for empty blocks/functions)
- Status: completed
- Goal:
  - Empty block with no final expression evaluates to **no value** (internal sentinel), not `0`.
  - Functions with no `->` and no final expression return nothing; backend maps to C `void`.
- Primary files likely to change:
  - `frontend/src/core/cte_value.h` (add internal no-value sentinel in CTE representation)
  - `frontend/src/transform/evaluator.cpp` (empty-block behavior)
  - `frontend/src/transform/evaluator_call.cpp` (void/no-value call propagation)
  - `frontend/src/transform/optimizer.cpp` / `frontend/src/transform/cte_value_utils.*` (if constexpr fact storage / bool conversion assumes scalar-only)
  - `frontend/src/transform/residualizer.cpp` (must not fold no-value into `0`)
  - `frontend/tests/...` (new regression tests; possibly fix expectations)
  - `examples/tutorial/reentrant.vx` or related examples only if any output changes (unlikely)
  - `docs/vexel-rfc.md` / docs text if current wording implies implicit zero (check)
- Notes:
  - No Vexel `#void` type is added.
  - Internal sentinel is allowed in CTE only.
  - Implementation notes (actual changes made):
    - Added internal CTE sentinel `CTNoValue`.
    - Empty blocks now evaluate to `CTNoValue` instead of integer zero.
    - Residualizer no longer materializes no-value facts as literals.
    - Type-use validator now distinguishes internal no-value functions from unresolved external return types.
    - RFC wording updated to state that empty blocks have no type and no implicit zero.
  - Tests/regressions to run for this step:
    - targeted frontend typechecker tests:
      - `TC-CONCRETE-USAGE/internal_no_value_return_used_error`
      - `TC-CONCRETE-USAGE/var_init_uses_void_error` (external unspecified return unchanged)
    - backend regressions:
      - `backends/vexel/tests/test.sh` (new lowered no-implicit-zero check)
      - `backends/c/tests/run_tests.py` (new CX-082 checks C `void` + no `return 0`)
  - Regression note:
    - Full `frontend` test suite currently has an unrelated/stale expectation mismatch in `tests/expressions/EX-032/sorted_no_mutation` (pre-existing with current branch state; not part of this step’s behavior change).

### Step 2 — Parser top-level boundary fix for plain `&` after semicolon-optional global initializers
- Status: completed
- Goal:
  - `&add(a,b){a+b} res=add(1234,2) &main(){}` parses as two top-level declarations without requiring `;`.
- Primary files likely to change:
  - `frontend/src/parse/parser.h` (parser mode/lookahead helpers if needed)
  - `frontend/src/parse/parser.cpp` (top-level initializer parse boundary disambiguation)
  - `frontend/tests/parse/...` and/or `frontend/tests/declarations/...` (regression tests)
  - `docs/vexel-rfc.md` (only if grammar wording must clarify semicolon-optional boundary rule)
- Notes:
  - This is a parser decision fix, not a language semantic change.
  - Implementation notes (actual changes made):
    - Added parser lookahead to detect a plain `&` function declaration shape at top level.
    - Enabled a top-level global-initializer boundary mode so the bitwise-`&` parser stops at the outermost initializer expression when the next token sequence is a plain function declaration.
    - This preserves semicolon-optional top-level style without changing expression semantics.
  - Tests/regressions run:
    - Repro program (`res=...` followed by `&main(){}` without `;`) now compiles.
    - New frontend regression: `declarations/DC-132/top_level_plain_ampersand_boundary`.
    - Existing bitwise precedence regression: `expressions/EX-074/bitwise_and_precedence`.
    - Backend smoke/regression suites rerun (`backends/vexel/tests/test.sh`, `backends/c/tests/run_tests.py`).

### Step 3 — Arbitrary-size integer literals and arbitrary-width integer values (frontend single source of truth)
- Status: completed
- Goal:
  - Literals can exceed 64-bit.
  - Integer widths `#iN/#uN` are fully respected in parsing, typing, and representability.
  - Unresolved literals remain unresolved until context (strict representability).
- Primary files likely to change (broad):
  - `frontend/src/parse/lexer.h`, `frontend/src/parse/lexer.cpp` (literal token payload strategy)
  - `frontend/src/core/ast.h`, `frontend/src/core/ast.cpp` (AST literal representation)
  - `frontend/src/core/cte_value.h` and supporting utils (arbitrary-precision integer representation)
  - `frontend/src/transform/evaluator*.cpp` (integer arithmetic, casts, shifts, bitwise ops)
  - `frontend/src/type/typechecker*.cpp` (type inference, representability, coercion, strictness interactions)
  - `frontend/src/transform/residualizer.cpp` (re-materialize large literals without truncation)
  - `frontend/src/analysis/*` or optimizer facts code if CT values are hashed/compared by fixed-width assumptions
  - Extensive `frontend/tests/*` and backend conformance tests
  - `docs/vexel-rfc.md` (literal semantics, representability, unresolved literal behavior)
- Notes:
  - This is the largest dependency step; CTE and backend work depend on it.
  - RCA (current behavior):
    - Integer literals overflow in the lexer (`stoll` / `stoull`) before contextual typing.
    - AST integer literal payload is fixed-width (`Expr::uint_val`), so exact literal values above 64 bits cannot survive parsing.
    - CTE integer values are fixed-width (`CTValue` only `int64_t/uint64_t`), so arithmetic, casts, and range/array-size queries truncate or fail beyond 64 bits.
    - Typechecker representability checks read fixed-width literal/CTE values and therefore cannot enforce arbitrary-width semantics soundly.
    - Residualizer reconstructs literals from fixed-width CTE values, so exact large constants cannot round-trip.
  - Implementation strategy (Step 3 scope, frontend only; backend lowering remains Step 5):
    - Use a frontend-owned arbitrary-precision integer representation for exact integer math (chosen implementation: wrapper over `boost::multiprecision::cpp_int` to avoid inventing a new bigint engine in this step).
    - Keep unresolved integer literals exact until constrained by type context; do not reintroduce smallest-fitting fallback.
    - Preserve strict representability diagnostics at the point where a concrete integer type is required.
  - Step 3 exact file plan (refresh before coding):
    - `frontend/src/core/apint.h` (new)
      - frontend-owned exact integer wrapper and helpers: parse literal text, sign/magnitude ops, representability checks, typed wrap/truncate/sign-extend semantics.
    - `frontend/src/core/ast.h`, `frontend/src/core/ast.cpp`
      - store exact integer literal payload on `Expr` (or exact raw + parsed APInt cache) without 64-bit truncation.
    - `frontend/src/parse/lexer.h`, `frontend/src/parse/lexer.cpp`
      - stop hard-overflowing integer tokens in lexer; preserve raw literal text, parse exact integer with APInt-aware path.
    - `frontend/src/parse/parser.cpp`
      - build integer literal AST nodes from exact literal payload (no `stoll` assumptions).
    - `frontend/src/core/cte_value.h`, `frontend/src/core/cte_value.cpp`
      - extend `CTValue` with exact integer variant and clone support.
    - `frontend/src/core/cte_value_utils.*`
      - bool conversion for exact integers.
    - `frontend/src/transform/evaluator.cpp`
      - exact integer literal evaluation, `to_int`/casts/coercions updated to work with exact integers.
    - `frontend/src/transform/evaluator_binary.cpp`
      - exact integer arithmetic/bitwise/shifts/comparisons on arbitrary precision; typed coercion still enforced by frontend type rules.
    - `frontend/src/transform/evaluator_assignment.cpp`
      - compound/plain assignment arithmetic paths upgraded to exact integers.
    - `frontend/src/transform/evaluator_cast.cpp`
      - exact integer cast/pack/unpack paths (including bounds/bit-width logic).
    - `frontend/src/type/typechecker_expr.cpp`
      - literal inference / representability / unsigned-bit ops use exact values.
    - `frontend/src/type/typechecker_expr_control.cpp`
      - range bounds, array-size checks, assignment constexpr fact tracking use exact values.
    - `frontend/src/type/typechecker_types.cpp`
      - compile-time array size validation reads exact values (then bounds-checks against frontend limits).
    - `frontend/src/transform/residualizer.cpp`
      - materialize exact integer CTE values back into AST literals without truncation.
    - `frontend/src/transform/optimizer.cpp`, `frontend/src/transform/evaluator_internal.h`
      - CTValue equality/classification helpers updated for exact integers.
    - Tests/docs:
      - `frontend/tests/lexer/*`, `frontend/tests/typechecker/*`, `frontend/tests/expressions/*` for >64-bit literals and exact representability.
      - `docs/vexel-rfc.md` clarify exact literal behavior (no lexer overflow before typing; contextual representability).
  - Note:
    - `boost::multiprecision::cpp_int` is available in the current environment (verified locally). If CI portability becomes an issue, revisit with a vendored frontend-only bigint implementation; do not regress semantics.
  - Implementation notes (actual changes made):
    - Added frontend-owned exact integer wrapper `APInt` (`boost::multiprecision::cpp_int`) and threaded exact integer payloads through AST literals.
    - Lexer/parser now preserve integer literal text and parse exact values without host-width overflow.
    - `CTValue` now supports `CTExactInt`; evaluator, casts, assignments, and residualizer materialization support exact integers.
    - Compile-time integer coercion/casts now support arbitrary widths (frontend semantics); fixed-width wrap/sign behavior remains enforced at concretization.
    - Local array materialization and array-size validation now accept compile-time exact integers when the realized size is host-materializable.
    - Residualizer structural equality was extended for unary/cast/binary/member/length nodes to avoid non-converging fixpoint loops when folding negative constants.
  - Tests/regressions run for this step:
    - New frontend regressions:
      - `expressions/EX-102/exact_integer_cte_arithmetic`
      - `expressions/EX-103/exact_integer_to_byte_array_cast`
      - `expressions/EX-104/local_array_size_from_exact_integer`
      - `errors/ER-016/u128_literal_overflow`
    - Existing regressions checked:
      - `errors/ER-012/i32_wrap_positive` (caught residualizer non-convergence regression; fixed)
    - Backend/frontend regressions:
      - `python3 backends/c/tests/run_tests.py` ✅ (210)
      - `make backend-vexel-test` ✅
      - `make backend-megalinker-test` ✅
      - `make docs-check` ✅
      - `make frontend-test` → known pre-existing mismatch at `expressions/EX-032/sorted_no_mutation` (unchanged)

### Step 4 — CTE performance + correctness for recursive numeric workloads (fib, large compile-time workloads)
- Status: completed
- Goal:
  - Remove obvious exponential blowups where call-instance memoization is semantically valid.
  - Preserve exact semantics for path-sensitive compile-time execution.
- Notes:
  - Must align with arbitrary-width integer CT values from Step 3.
  - RCA (current behavior):
    - `CTEEngine` reuses a `CompileTimeEvaluator` instance, but each query resets evaluator state (`reset_state()`), and there is no per-query call memoization.
    - Recursive compile-time functions (e.g. Fibonacci) therefore re-evaluate identical call instances repeatedly inside a single query, producing exponential blowups.
    - Optimizer scheduler caching (`stable_values_`) works at expression/root granularity across fixpoint iterations, but it does not eliminate repeated recursive subcalls that occur during one `CompileTimeEvaluator::query(...)`.
    - Memoization is not universally safe:
      - local/nested functions may capture caller locals (ambient state beyond arguments),
      - expression parameters (`$`) depend on caller expressions/side effects,
      - cross-query reuse would need a seed-symbol/environment fingerprint.
    - Therefore Step 4 should add **per-query** memoization only for a clearly sound subset (top-level/internal, non-capturing-by-construction, non-expression-parameter calls).
  - Step 4 execution plan (exact files / why):
    - `frontend/src/transform/evaluator.h`
      - add per-query call memo state (result cache + in-progress guard) owned by the evaluator and cleared by `reset_state()`.
    - `frontend/src/transform/evaluator_call.cpp`
      - implement memo-key construction and memo lookup/store around function body evaluation.
      - gate memoization to sound calls only (no externals, no local functions, no expression params, memoizable value arguments/receivers).
      - preserve argument evaluation side effects by memoizing only after argument/receiver evaluation and coercion.
    - `frontend/src/core/cte_value.h/.cpp` or local helpers in `evaluator_call.cpp`
      - (only if needed) helper(s) for strict CT value serialization/hashing used by memo keys, including `CTExactInt`.
    - `frontend/tests/expressions/*`
      - add recursive CTE folding regression (fib) to prove semantics and exercise memoization path.
      - add nested/local capture regression to guard against unsound memoization of captured functions.
    - `TODO.md`
      - record measured timings and mark Step 4 completed after validation.
  - Validation plan:
    - targeted frontend regressions for new tests
    - `make frontend-test` (expect only known `EX-032` mismatch unless new regressions appear)
    - `python3 backends/c/tests/run_tests.py`, `make backend-vexel-test`, `make backend-megalinker-test`, `make docs-check`
    - benchmark compile time on a generated Fibonacci sample (timing report only; no brittle timing assertions)
  - Implementation notes (actual changes made):
    - Added per-query call memoization state to `CompileTimeEvaluator` (cleared by `reset_state()`).
    - Memoization is applied only to a sound subset:
      - internal (non-external) functions,
      - no expression parameters,
      - no ambient local bindings outside the current call frame (conservative capture-safety gate),
      - memoizable scalar/string receiver/argument values.
    - Memo keys include exact integer values (`CTExactInt`) and preserve strict value identity for memoized call instances.
    - Cached results are stored/restored via `clone_ct_value(...)` to avoid aliasing mutable aggregate storage across memo hits.
    - Added in-progress call-key guard to avoid recursive self-hit misuse (no partial-result reuse).
  - Regressions/tests run:
    - New frontend regressions:
      - `expressions/EX-105/recursive_fib_cte_fold`
      - `expressions/EX-106/local_capture_changes_between_calls` (guards against unsound memoization of captured nested functions)
    - `make frontend-test` → known pre-existing mismatch at `expressions/EX-032/sorted_no_mutation` (unchanged)
    - `python3 backends/c/tests/run_tests.py` ✅ (210)
    - `make backend-vexel-test` ✅
    - `make backend-megalinker-test` ✅
    - `make docs-check` ✅
  - Timing snapshot (CLI end-to-end, process startup included; generated recursive fib sample compiled with `-b vexel`):
    - `fib(100)` ~ 2.75 ms
    - `fib(200)` ~ 2.49 ms
    - `fib(400)` ~ 2.65 ms
    - `fib(800)` ~ 3.13 ms

### Step 5 — Arbitrary-width integer lowering in C backend (full arithmetic) + megalinker backend (full arithmetic)
- Goal:
  - Both backends support arbitrary-width integer types/ops, with fast path for 8/16/32/64 and generic helpers otherwise.
- Primary files likely to change (C backend):
  - `backends/c/src/codegen_support.cpp/.h` (type mapping)
  - `backends/c/src/codegen.cpp`
  - `backends/c/src/codegen_expr.cpp`
  - `backends/c/tests/*` (new backend tests, conformance coverage)
- Primary files likely to change (megalinker backend):
  - `backends/ext/megalinker/src/codegen_support.cpp/.h`
  - `backends/ext/megalinker/src/codegen.cpp`
  - `backends/ext/megalinker/src/codegen_expr.cpp`
  - `backends/ext/megalinker/src/megalinker_backend.cpp` (runtime/helper emission, help text if flags added)
  - `backends/ext/megalinker/tests/*`
- Docs:
  - Backend READMEs/help output docs if any limitations are removed.

### Step 6 — Playground runtime execution study/implementation (optional feature, full Vexel execution target)
- Goal:
  - If implemented, run Vexel in-browser with actual Vexel semantics and show runtime console output.
- Primary files likely to change:
  - `playground/playground.template.html`
  - `playground/embed.py`
  - `playground/Makefile`
  - Possibly frontend runtime/VM exposure files (if reusing evaluator/VM in WASM)
  - `docs/index.html` and `playground/README.md`
- Notes:
  - This step is optional by feature priority, but if done must target full execution semantics (not a fake stub).

### Step 7 — Compound assignment operators (single coherent design, include logical &&= / ||=)
- Status: completed
- Goal:
  - Implement compound assignments coherently in one pass, including:
    - arithmetic: `+= -= *= /= %=`
    - bitwise: `&= |= ^=`
    - shifts: `<<= >>=`
    - logical: `&&= ||=`
- RCA (current behavior):
  - Lexer/parser do not tokenize or parse compound assignments, so all `op=` forms fail lexically/parsing.
  - AST assignment nodes already carry a generic `op` string field, but assignment creation/printing/codegen currently hardcode plain `=`.
  - Typechecker assignment logic only validates plain assignment and mutability; no operator-specific compound rules exist.
  - CTE assignment evaluator only supports plain assignment and always evaluates RHS first; this is incompatible with logical short-circuit (`&&=` / `||=`).
  - C/megalinker codegen emit plain `lhs = rhs`; C has no native `&&=`/`||=` operators, so backend handling is required.
- Primary files likely to change:
  - `frontend/src/parse/lexer.h/.cpp` (tokens)
  - `frontend/src/parse/parser.cpp` (parse + desugar strategy)
  - `frontend/src/core/ast.*` (only if new expr kind is needed; prefer desugaring to existing assignment/binary nodes)
  - `frontend/src/type/typechecker*.cpp` (type rules if desugaring is not fully transparent)
  - `frontend/src/transform/evaluator*.cpp` (single-evaluation semantics if parser/lowerer desugars via temporaries)
  - `frontend/src/transform/lowerer.cpp` (likely best place for safe desugaring if parser keeps syntax-level node)
  - `frontend/tests/*` (especially member/index side-effect and short-circuit for `&&=`/`||=`)
  - `docs/vexel-rfc.md` (operator grammar + semantics)
- Notes:
  - `&&=` / `||=` must preserve short-circuit semantics and single lvalue evaluation.
  - Step 7 execution plan (exact files / why):
    - `frontend/src/parse/lexer.h`, `frontend/src/parse/lexer.cpp`
      - add compound-assignment tokens and lexing for all `op=` forms.
    - `frontend/src/parse/parser.cpp`
      - extend `parse_assignment()` to accept all compound-assignment tokens with right-associative parsing.
    - `frontend/src/core/ast.h`, `frontend/src/core/ast.cpp`
      - preserve assignment operator on AST nodes (plain `=` and compounds) through `Expr::make_assignment`.
    - `frontend/src/type/typechecker_expr_control.cpp`
      - add compound-assignment operator validation (numeric/bitwise/shift/logical), reject declaration-style compound assignment.
      - ensure constexpr fact tracking uses the full assignment expression result for compound ops.
    - `frontend/src/transform/evaluator_assignment.cpp`
      - implement compile-time execution for compound assignments, including RHS short-circuit for `&&=`/`||=`.
    - `backends/vexel/src/vexel_backend.cpp`
      - print compound assignments in lowered Vexel output.
    - `backends/c/src/codegen_expr.cpp`, `backends/ext/megalinker/src/codegen_expr.cpp`
      - emit compound assignments in generated C; special handling for `&&=`/`||=` since C lacks those operators.
    - Tests:
      - `frontend/tests/lexer/*` (operator tokenization smoke)
      - `frontend/tests/expressions/*` (parsing/precedence/chaining)
      - `frontend/tests/typechecker/*` (logical bool constraints on `&&=`/`||=`)
      - `backends/c/tests/backend_c/*` (runtime semantics, short-circuit)
      - optional megalinker compile smoke test if coverage gap is exposed
    - Docs:
      - `docs/vexel-rfc.md` (assignment operators list and semantics)
  - Implementation notes (actual changes made):
    - Added lexer tokens for all compound assignments, including `&&=` / `||=`.
    - Parser now parses compound assignments right-associatively and stores the exact assignment operator on AST assignment nodes.
    - Typechecker validates compound assignments by operator family (arithmetic/bitwise/shifts/logical), rejects declaration-style compound assignments, and tracks constexpr facts using the full compound expression result.
    - Compile-time evaluator executes compound assignments (including `&&=` / `||=` short-circuit).
    - Vexel backend round-trips compound assignments in lowered output.
    - C and megalinker backends emit native C compound ops where available and lower `&&=` / `||=` via explicit short-circuit control flow with an addressed lvalue temp (single lhs evaluation).
  - Tests/regressions run:
    - Frontend:
      - `EX-100/compound_assignments_roundtrip` (parse + lowered output preserves operators)
      - `EX-101/compound_assignments_cte_semantics` (CTE folds compound semantics to constant)
      - `TC-LOGICAL-BOOL/compound_assignment_non_boolean_operands` (rejection path)
    - Backend C:
      - `python3 backends/c/tests/run_tests.py` (210 tests, incl. new `CX-083`)
    - Backend smoke/regression:
      - `make backend-vexel-test`
      - `make backend-megalinker-test`
    - Docs check:
      - `make docs-check`

### Step 8 — Megalinker temp/reentrant code-shape audit and cleanup (strategy-ready, backend-owned)
- Goal:
  - Clean temp declarations/reuse and clarify reentrant vs nonreentrant emission without freezing future optimization strategy.
- Primary files likely to change:
  - `backends/ext/megalinker/src/codegen.h`
  - `backends/ext/megalinker/src/codegen.cpp`
  - `backends/ext/megalinker/src/codegen_expr.cpp`
  - `backends/ext/megalinker/src/codegen_support.cpp`
  - `backends/ext/megalinker/src/megalinker_backend.cpp` (flags/help/comments if strategy knobs added)
  - `backends/ext/megalinker/tests/*`
  - `examples/tutorial/banked.vx` (if used as regression sample)
- Notes:
  - Keep backend-specific freedom. C and megalinker code shape is intentionally allowed to drift.

- It would be nice that the playground would have e.g. a console so that we could run programs and see the output. (very much optional, I understand this is hard)
  - RCA:
    - The current playground is a compiler playground, not a runtime playground. It embeds the compiler (WASM) and runs compilation only.
    - `playground/playground.template.html` exposes compile/debug output and generated files, but no execution path or stdout capture.
    - `--run` / `--emit-exe` rely on `libtcc` in the native driver, which is not available in the browser build.
    - Browser-side "run" would require a runtime path separate from C backend emission (e.g. Vexel VM/interpreter in WASM, or a second compiler/runtime in-browser).
  - Plan:
    - Phase 1 (small): add a UI console pane only for compiler logs/stdout separation (compile logs vs runtime output placeholder).
    - Phase 2 (real): choose a browser execution model:
      - Vexel VM/interpreter in WASM (frontend/runtime-owned semantics), or
      - JS host shim for a constrained subset (not ideal, risks semantic drift), or
      - Server-side compile/run (breaks client-side requirement).
    - Phase 3: define sandboxed imported I/O (e.g. `putchar`, `putstr`) and capture output into console.
  - Questions:
    - Do you want "run" to mean actual Vexel execution in-browser (preferred), or only a tiny host-stubbed subset?
    - Should runtime support start with only `&!putchar(c:#u8)` capture, or do you want a slightly broader standard host shim?
    - Is a Web Worker required from the start for responsiveness/cancel?

- c and megalinker backends should support arbitrary length integers, of course shortcuts may be taken for c standard sizes (8, 16, 32), but they should support the other ones.
  - RCA:
    - Frontend syntax/type system already accepts arbitrary integer widths (`#iN/#uN`), but backend lowering does not.
    - C backend hard-rejects widths outside `8/16/32/64` in `backends/c/src/codegen_support.cpp`.
    - Megalinker backend has the same hard-reject path in `backends/ext/megalinker/src/codegen_support.cpp`.
    - Backend constant emission and codegen internals also assume `int64_t/uint64_t` in many places.
  - Plan:
    - Do not patch backend width switches in isolation; first complete frontend arbitrary-literal/arbitrary-integer value model (next TODO items).
    - Then backend strategy in two layers:
      - Native fast path for `8/16/32/64`.
      - Generic path for any other width using backend-owned lowered representation + helper ops.
    - Keep C and megalinker independent (intentional drift): same semantics, different lowering style/runtime helpers.
  - Questions:
    - For non-native widths, do you want full arithmetic support immediately, or is "storage + moves + comparisons + casts" acceptable for a first milestone?
    - For C backend generic path, do you prefer portable limb structs/helpers (C11), or opportunistic `_BitInt(N)` when compiler supports it (with fallback)?
    - Should backend ABI expose non-native widths directly (struct/array wrapper), or only internally for now?

- literals may be of arbitrary size
  - RCA:
    - Numeric literals are parsed into fixed-width token storage (`frontend/src/parse/lexer.h`: `Token.value` is `int64_t/uint64_t/double/string`).
    - AST literal storage is also fixed-width (`frontend/src/core/ast.h`: `Expr::uint_val` is `uint64_t`).
    - Compile-time value representation (`frontend/src/core/cte_value.h`) is fixed-width for integers (`int64_t/uint64_t`).
    - Result: lexer overflow occurs before type/context can help (e.g. `#u128` initializer literal currently errors during lexing/parsing pipeline).
  - Plan:
    - Introduce a frontend-owned arbitrary-precision integer representation for literals/CTE values (single source of truth).
    - Keep literal lexeme text as source of truth until type/context resolution decides signedness/width and representability.
    - Refactor literal handling across:
      - lexer token payload (or store raw lexeme only),
      - AST literal node payload,
      - CTE arithmetic/coercion,
      - typechecker representability checks,
      - residualizer literal reconstruction.
  - Questions:
    - For unresolved integer literals, should the frontend carry exact mathematical integer values (signless magnitude + sign flag) until contextual typing? (recommended)
    - On overflow after contextual typing, should diagnostics always cite both target type and original literal text? (recommended)

- fibonacci example is very slow on CTE, and directly crashes for very large numbers on the web playground.
  - RCA:
    - Current CTE is still an AST interpreter (`frontend/src/transform/evaluator*.cpp`), not a compiled VM.
    - Recursive pure call evaluation has no general call-instance memoization (`frontend/src/transform/evaluator_call.cpp`).
      - The existing cache is only for constant symbols (`constant_value_cache`), not arbitrary function calls with arguments.
    - Recursive evaluation is capped by `MAX_RECURSION_DEPTH = 1000` (`frontend/src/transform/evaluator.h`).
    - Web playground compiles on the UI thread and has no cancellation/worker isolation, so long CTE runs can freeze or crash the page.
    - Large Fibonacci values also collide with fixed-width integer limits (ties into arbitrary-literal/arbitrary-integer TODOs).
    - Empirical check (CLI): `fib(35)` compile-time fold currently takes ~10s on this machine, confirming exponential behavior.
  - Plan:
    - Split the problem into 3 tracks (do not mix):
      - CTE algorithmic performance (memoization / execution engine improvements),
      - numeric capacity (arbitrary precision),
      - web UX containment (worker + cancellation + limits).
    - For CTE performance: add pure-call memoization keyed by function instantiation + receiver/arg values + relevant facts.
    - For web: move compilation into a Web Worker and add cancel/time budget UI.
  - Questions:
    - Should recursive pure calls be memoized by default whenever arguments are compile-time-known? (recommended)
    - Do you want the playground to hard-timeout/cancel long compiles, or only warn and let them run?
    - Is preserving exact recursive semantics with memoization (same result, faster) sufficient, or do you want additional recursion diagnostics?

- &^main() { } -> seems to default to uint64_t, there should be either an error or void.
  - RCA:
    - Current behavior is caused by CTE semantics for empty blocks: `CompileTimeEvaluator::eval_block` returns integer `0` when a block has no `result_expr` (`frontend/src/transform/evaluator.cpp`).
    - The optimizer/residualizer can fold empty function bodies using that fact, which materializes an implicit `0` in lowered output (`-b vexel` shows this).
    - This is a semantic leak from the evaluator into language behavior: an empty block is being treated as an integer expression.
    - The backend then emits a non-void return type/value because the body now appears to return a value.
  - Plan:
    - Redefine empty block evaluation as "no value" (void-like), not numeric zero.
    - Ensure residualization does not replace empty blocks with `0`.
    - Tighten typechecker/backend invariants so empty-body functions become:
      - explicit `void` if allowed by language semantics, or
      - a frontend error if a value-returning function is required.
  - Questions:
    - Language choice: should any function with no final expression and no `->` be inferred as `void`? (recommended)
    - For exported `&^main()` specifically, do you want:
      - allowed and inferred `void`, or
      - frontend error requiring explicit `-> #i32` (or explicit `-> void` if/when syntax exists)?

- "&add(a,b){a+b} res=add(1234,2) &^main(){}" compiles, but "&add(a,b){a+b} res=add(1234,2) &main(){}" does not.
  - RCA:
    - This is a parser boundary ambiguity caused by optional semicolons + plain `&` being both:
      - a top-level function declaration starter token, and
      - the bitwise-and operator in expressions.
    - `&^` and `&!` are distinct tokens, so they do not collide with expression parsing.
    - In the failing case, the parser continues parsing `res=add(1234,2) & main()` as part of the previous global initializer instead of starting a new top-level function declaration.
    - Root ownership is parser grammar/statement-boundary policy, not typechecker/backend.
  - Plan:
    - Decide syntax policy first:
      - keep semicolon-optional top-level globals and add parser disambiguation for plain `&`, or
      - require semicolon before a following plain `&` top-level function declaration, or
      - make top-level semicolons mandatory (largest syntax change).
    - Implement as a parser-level boundary rule, not a post-parse heuristic.
  - Questions:
    - Do you want to preserve semicolon-optional style at top level? (recommended if possible)
    - If yes, is a targeted parser disambiguation for plain `&` acceptable, or do you prefer a stricter syntax rule requiring `;` before `&main()`?

- When I remove all symbol annotations from the sieve example, it still compiles.
  - RCA:
    - This is mostly current policy, not necessarily a bug:
      - default CLI mode is `--type-strictness 0` (relaxed),
      - local variables can be inferred from initializers/usage,
      - many sieve locals are inferable.
    - `TypeChecker::check_var_decl` only enforces explicit local annotations at strictness level `>= 1` (`frontend/src/type/typechecker.cpp`).
    - Exported/public ABI boundaries are still stricter (e.g. exported function return type must be concrete).
    - There may still be hidden inference edge cases worth auditing, but the baseline behavior is intentional under strictness 0.
  - Plan:
    - Clarify policy in docs/tutorial/examples:
      - relaxed inference by default,
      - `--type-strictness` for stricter projects.
    - Decide whether playground should expose/select strictness level (currently likely hidden from UI).
    - Add targeted tests for "strictness levels reject/accept same example" to lock intended behavior.
  - Questions:
    - Should default strictness remain `0`, or should we move default CLI/playground behavior to `1`?
    - Do you want tutorial examples to prefer explicit annotations even when inference is allowed (for pedagogy/readability)?

- it seems that assignment operators (e.g., +=) are not yet implemented?
  - RCA:
    - Correct: parser/lexer do not define compound assignment tokens/operators.
    - `TokenType` has only `Assign`, and `parse_assignment()` handles only `=`.
    - Current parse failure also produces cascading errors because `+` is parsed, then `=` becomes unexpected.
  - Plan:
    - Add lexer tokens for compound assignments (`+=`, `-=`, `*=`, `/=`, `%=` etc. as decided).
    - Parse them in `parse_assignment()` and desugar in frontend to explicit assignment with single-evaluation semantics.
    - Ensure lvalue subexpressions are evaluated exactly once (important for `a[i()] += f()` and member/index targets).
    - Typechecker/CTE should operate on desugared form to avoid duplicating semantics.
  - Questions:
    - Which operators should be included in first pass: arithmetic only, or also bitwise/shift/logical?
    - Should compound assignment return the assigned value (like `=` currently does)? (recommended for consistency)
    - Should overloaded operators participate (`a += b` uses overloaded `+` where available)?

- weird declaration of tmp variables in "reentrant" functions (banked.vx example). We need to rething all that reentrant / global variables.... it may prevent optimizations / reuse of variables. We might want to couple the output togetehr with sdcc.
  - RCA:
    - Confirmed in generated megalinker C: reentrant variants can produce noisy temp declarations and nested inlined temps (see generated `megalinker/*.c` for tutorial/banked-style examples).
    - Root cause is architectural:
      - recursive expression codegen emits temps on the fly,
      - temp allocation/reuse is heuristic and local to emission recursion (not a dedicated scheduling/lifetime pass),
      - reentrant vs nonreentrant ABI mode is controlled by flags in shared code paths, so temp strategy is not clearly separated by ABI mode.
    - This is primarily a megalinker backend code-shaping issue, not frontend semantics.
  - Plan:
    - Separate concerns inside megalinker backend:
      - expression lowering order,
      - temp lifetime/reuse policy,
      - ABI frame strategy (reentrant vs nonreentrant),
      - bank-loading strategy.
    - Add backend-specific tests that assert shape properties (e.g., no duplicate temp declarations in a scope, no avoidable temps in simple expressions) without overfitting exact formatting.
    - Optionally add an SDCC inspection loop later (compile generated C and compare simple metrics), but only after codegen structure is stabilized.
  - Questions:
    - What is the immediate priority: readability of generated C, SDCC output quality, or minimizing RAM/stack/temp count?
    - Do you want a hard separation of emit style for reentrant vs nonreentrant functions now (two code paths/policies), or an incremental cleanup within current shared emitter?
    - Is it acceptable to add backend-only heuristics based on SDCC behavior, as long as semantics remain backend-owned and documented in megalinker README/help?
