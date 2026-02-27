# NEXT_STEPS (execution contract + working log + feature plan)

## Execution Instructions (mandatory)

This document is the authoritative working log for execution. The workflow must follow these rules:

1. **No scope reduction.** It is not acceptable to narrow semantics to make progress look faster.
2. **Intent first.** For every change, preserve language intent and previously agreed semantics.
3. **Hard semantic decisions are resolved inline.** No defer/delay placeholders.
4. **Decision protocol (mandatory) for hard semantics:**
   - root-cause analysis and blast-radius analysis
   - at least one concrete recommendation that preserves intent
   - pick and implement a decision in the same pass
   - record decision and rationale in this file
5. **No shortcuts/bridges.** No compatibility hacks that violate architecture ownership or contracts.
6. **Re-architecture when needed.** If local edits cannot satisfy intent cleanly, perform structural refactor.
7. **Pass discipline for each step:**
   - root-cause analysis
   - file-level plan
   - implementation
   - tests/regressions
   - docs/RFC updates
   - consistency audit
   - commit
   - update this working log
8. **Coverage discipline.** Add/adjust tests to capture behavior and prevent regression.
9. **Finalization discipline.** After all planned work is done:
   - full audit of code + docs + RFC + examples + tutorial/playground consistency
   - fix all mismatches found
   - run full test suite again
10. **This file stays live.** Status and decisions are updated continuously as work proceeds.

## Status Tracker (single source of truth)

Legend: `[ ]` pending, `[~]` in progress, `[x]` complete.

- [x] Feature 1: per-element operators stage 1A/1B (syntax, lowering, broadcasting, regressions)
- [~] Feature 2: fixed-point (`#uI.F/#iI.F`) full completion
  - [x] parser/type plumbing (`I+F>0`, `F<0`, type storage, mangling/stringification)
  - [x] native + non-native cast support (frontend/CTE/C/megalinker)
  - [x] non-native non-zero-fraction `* / %` + compound support
  - [ ] bitwise/shift semantics for `F != 0` (or explicit final-language rule + enforcement)
  - [ ] signed fixed-point bitwise/shift semantics (or explicit final-language rule + enforcement)
  - [ ] 64-bit fixed-point `* / %` semantics/backend support
- [x] Feature 3: std module system (`std/` fallback + local override resolution)
- [~] Feature 4: compiler-recognized `std::math`
  - [x] scalar surface + CTE fold + backend libc mapping
  - [x] array lifting + broadcasting behavior
  - [ ] final semantic lock + documentation consistency sweep
- [ ] Feature 5: native vector/matrix language support (independent feature)
- [ ] Final pass: full code+docs audit and mismatch remediation

## Working Log Entries

### 2026-02-27
- Established explicit execution contract (no scope reduction, no deferral on hard semantics).
- Set this file as the authoritative status tracker and decision log.
- Open priorities remain fixed-point completion (Feature 2 gaps), math semantic lock cleanup, and Feature 5.

## Progress notes (local, temporary context)

Implemented so far:
- `std/` bundled-module fallback with per-module local override (compiler/module loader)
- Per-element operator syntax stage 1A (dotted binary operators with scalar-precedence parsing)
  - dotted compound assignments (`.+=`, `.||=`, ...) lower via `lhs = lhs .op rhs`
  - v1 restriction: dotted compound assignment target must be side-effect-free
- Per-element operator stage 1B (frontend broadcasting rewrite for array operands)
  - strict broadcasting (trailing-dimension alignment, singleton expansion, scalar lifting)
  - incompatible shapes are compile-time errors
  - current frontend lowering requires side-effect-free operands (broadcast expansion duplicates indexed scalar expressions)
  - preserves dotted operator leaves for named-type element overloads (e.g. `#T::.+`) while lowering primitive leaves to scalar builtins (`.+` -> `+`, etc.)
- `std::math` phase 1 scalar surface:
  - bundled module declarations (`std/math.vx`)
  - frontend compile-time folding for supported scalar functions
  - C / megalinker backend libc symbol mapping for bundled `std::math` externs
  - local `std/math.vx` overrides remain true overrides (builtin folding/mapping only for bundled std origin)
  - classification helpers (`isnan`, `isinf`, `isfinite` and `f32` suffixed variants)
  - array lifting in frontend for bundled `std::math` unary/binary calls with strict broadcasting
    (trailing-dimension alignment + singleton expansion; current lowering requires side-effect-free arguments)
  - robustness guards:
    - architecture test keeps bundled `std/math.vx` surface in sync with frontend CTE folding and both backend libc maps
    - architecture test keeps bundled-origin gating (`ModuleOrigin::BundledStd`) present in frontend + both backends
    - scalar and array dynamic smoke tests generate calls across the full bundled unary/binary scalar surface and ensure constexpr inputs fold away before backend handoff
    - local `std/math.vx` override regressions cover both scalar and array-call paths (must not trigger builtin folding/array lifting)

Additional completed hardening (post-feature passes):
- Per-element operator lowering surface smoke test across the full primitive dotted operator/compound-assignment set (arrays + broadcasting) to catch parser/lowering drift.
- C backend runtime regression for array broadcasting through dotted compound assignments (including named-type dotted overload leaves).
- Fixed backend codegen bug (C + megalinker): runtime array literal temporaries were emitted as unconditional `static` aggregate initializers, generating invalid C when elements were runtime expressions.
  - Reentrant path now emits block-scope aggregate initialization.
  - Non-reentrant (static-frame) path now declares the temp and fills elements at runtime.
  - Added C backend runtime regressions and megalinker output-shape regressions for both reentrant and non-reentrant paths.
- Fixed-point hardening:
  - C/megalinker backends now support non-native non-zero-fraction fixed-point casts with scaling-aware lowering in extint paths.
  - Added a C backend runtime regression for extint-backed non-native non-zero-fraction fixed-point unary `-`, `+`, `-`, comparisons, and compound `+=` / `-=` using real stub-provided ABI values.

Notes from implementation:
- Vexel currently has no function overloading, so `std::math` uses C-style
  suffixed names for `#f32` (`sinf`, `sqrtf`, `powf`, ...).
- `std::bits` remains placeholder-only.
- Fixed-point foundation pass (syntax + plumbing):
  - parser/type parser accepts `#uI.F` / `#iI.F` with `I + F > 0` (including `F < 0`)
  - type system stores fractional bits in `Type` and preserves them in hashing/mangling/stringification
  - `vexel`, C, and megalinker backends map fixed-point storage to raw integer storage width (`I + F`) for ABI-visible declarations/signatures
  - frontend now allows fixed-point same-type pass-through assignment (`=` only), enabling wrapper/pass-through functions without arithmetic
  - frontend now allows a native-width (`I+F` in 8/16/32/64) same-type arithmetic subset: unary `-`, binary `+`/`-`, and comparisons
  - frontend now also allows native-width same-type compound `+=` / `-=`
  - frontend/backend/CTE support explicit casts among fixed/integer/bool/float primitives for native and non-native fixed-point widths
  - frontend/backend/CTE now support fixed-point `*`, `/`, `%`, `*=`, `/=`, `%=` for native storage widths up to 32 bits (`8/16/32`)
  - frontend/backend/CTE support same-type non-native non-zero-fraction fixed-point unary `-`, `+`, `-`, comparisons, and compound `+=` / `-=` (extint-backed raw semantics)
  - fixed-point bitwise/shift for `F != 0` and 64-bit fixed-point `* / %` remain open
  - regressions added for syntax acceptance, invalid non-positive total width, explicit unsupported-operation diagnostics, and C/megalinker ABI signature emission

## 1) Overloadable per-element operators (Matlab-style)

### Goal (as understood)
Allow defining operators distinct from normal operators for element-wise behavior (e.g. vector/matrix `.*` vs `*`), ideally covering a complete operator family (not only multiplication).

### Feasibility
**Feasible**, with a clear caveat:
- If this is **only overload dispatch syntax** (i.e. user-defined semantics via operator overload functions), cost is moderate.
- Adding **broadcasting semantics** (which is a natural outcome for usable per-element arithmetic) raises the cost materially because it introduces frontend shape rules and CTE shape expansion semantics.

Conclusion from discussion:
- This feature should be treated as a **staged plan**:
  - per-element operators first (same-shape semantics),
  - then broadcasting semantics with strict compile-time rules.

### Why it fits the current architecture
- Operator overloading already exists in the parser/typechecker as operator-name-based dispatch.
- Frontend can treat per-element operators as just **new operator tokens/strings**.
- Backends do not need special vector semantics if these operators lower to normal overload calls.

### Main technical challenge
The challenge is mostly **syntax + grammar**, not backend execution:
- `.` is already used for member access and floats.
- Adding tokens like `.*`, `./`, `.+`, `.-`, etc. requires careful lexer/parser disambiguation.

This is solvable, but must be done deliberately.

### Suggested semantic scope (first coherent version)
Treat per-element operators as **distinct operators** with normal precedence/associativity mirroring their scalar counterparts:
- Arithmetic: `.+ .- .* ./ .%`
- Bitwise: `.& .| .^ .<< .>>` (if desired)
- Comparison: `.== .!= .< .<= .> .>=`
- Logical: `.&& .||` (if desired; no short-circuit if element-wise semantics are expected)

Important note:
- Element-wise logical operators should likely be **non-short-circuit**, unlike scalar `&&` / `||`.
- That semantic distinction must be explicit in RFC/docs.

### Cost analysis
#### Engineering cost (syntax + overload dispatch only)
- **Frontend lexer/parser**: Medium
- **Typechecker/operator resolution**: Medium
- **CTE**: Low to Medium (only if per-element ops are used on scalar types or custom overloads fold)
- **Backends**: Low (mostly no special work if overloads lower as calls)
- **Tests/docs**: Medium

**Overall cost**: **Medium**

#### Engineering cost (broadcasting semantics added)
- **Frontend semantics/type rules/shape rules**: High
- **CTE container-wise execution**: High
- **Backend codegen optimizations**: Medium to High

**Overall cost**: **High** (good follow-on after syntax/same-shape stage)

### Architectural recommendation
Implement as a **staged per-element feature**:
- **Feature 1A**: overloadable per-element operators with same-shape semantics only.
- **Feature 1B**: broadcasting semantics for per-element operators (and later `std::math` array-aware binary functions), using strict compile-time shape rules.
- No hidden transpose/reshape/flatten behavior.
- No backend magic required for the syntax stage; broadcasting semantics are frontend/CTE work.

This preserves momentum while keeping the semantics explicit.

### Decisions / constraints (locked from discussion)
- Operator set scope is **complete**, i.e. whatever a user reasonably expects for the chosen per-element operator family (not a tiny subset).
- Per-element logical operators are **non-short-circuit**.
- Per-element compound assignments are **in scope** if syntax is lexer/parser-clean and unambiguous.
- If per-element compound logical assignments are included (e.g. `.&&=` / `.||=`), they should also be **non-short-circuit** element-wise operations.
- Broadcasting is **in scope** for the per-element feature, but should land as a staged follow-on after same-shape semantics.
- Broadcasting rule (proposed and accepted):
  - align shapes from trailing dimensions
  - each aligned dimension must be equal or one side must be `1`
  - result dimension is the maximum of the two
  - scalars are rank-0 and can broadcast to any shape
  - no implicit transpose / flatten / reshape
  - incompatible shapes are compile-time errors
- For per-element compound assignments, broadcasting applies to the **RHS only**; LHS shape is fixed and must match the broadcast result shape.
- Overload declaration syntax should be chosen for parser clarity; exact token spelling is secondary to clean disambiguation.

### Recommended delivery (this feature only)
#### Feature 1A — Pure overloadable per-element syntax (no native vector semantics)
- Add per-element operators as distinct overloadable operators only.
- Same-shape semantics only (no broadcasting yet).
- No hidden container semantics.
- Full audit + extensive tests + commit.

#### Feature 1B — Broadcasting semantics for per-element operators
- Add strict compile-time broadcasting rules (trailing-dimension alignment, `1` expansion, rank-0 scalar lifting).
- Apply the same rules to per-element binary operators and compound assignments.
- Keep broadcasting errors explicit (no best-effort reshaping).
- Full RFC shape-rule examples + CTE tests + commit.

- Native vector/matrix types remain a **separate independent feature** (see dedicated section below), not treated as a sub-step of per-element operators.

---

## 2) Fixed-point types as `#uI.F` / `#iI.F`

### Goal (as understood)
Add fixed-point types as a numeric family closely related to integers:
- `#uI.F` unsigned fixed-point
- `#iI.F` signed fixed-point
- total storage width = `I + F`
- leverage arbitrary-width integer machinery where possible

### Feasibility
**Feasible**, but **not a small extension** in semantic terms.

You are correct that representation can reuse integer infrastructure, but arithmetic semantics are not "just an annotation":
- multiplication/division need scaling rules
- rounding/truncation behavior must be defined
- promotion between different fractional widths must be defined
- casts to/from int/float must be defined

Refinement from discussion (important):
- The main challenge is not only "promotion", but **requirement tracking** for operations.
  - Example: `#u4.4 + #u4.12` may remain underconstrained and should error if no contextual target type constrains the operation result.
  - If assigned/cast into a concrete fixed-point type, that contextual requirement can define the operation result semantics.
  - This is not a fixed-point-specific exception; it should follow the general Vexel type-resolution policy (underconstrained expressions require explicit casts/annotations).
- Casts need a precise distinction between:
  - **numeric conversion** (value conversion), and
  - **bit reinterpretation/layout conversion** (if allowed at all).
  - Numeric cast `fixed -> int` can be defined as taking the integer part (truncate fractional component).
  - The reverse direction (`int -> fixed`) and fixed<->fixed with different scales is straightforward as numeric scaling.
  - Bit reinterpretation should not be overloaded onto numeric casts.
- For fixed-point with `F != 0`, it is reasonable to **disallow integer-style bitwise/shift operators** (`~ & | ^ << >>`) in v1.
- `F < 0` is **in scope for v1**, provided `I + F > 0`; this increases parsing/type semantics complexity but remains feasible.
- `%` is expected to be supported for fixed-point in v1, but its semantics must be written explicitly in the RFC (especially result scale / sign behavior).
- Literal policy now materially affects this feature (see dedicated subsection below): non-`e` literals are treated as exact fixed-point literals, and `e` literals are floating literals.

The representation reuse is real and valuable, but the semantic design work is substantial.

### What can be reused from current code
Strong reuse:
- Arbitrary-width integer storage / APInt in frontend
- Exact integer literal handling
- CTE integer machinery (as a base representation)
- Backend arbitrary-width integer helper paths (C + megalinker)

What cannot be reused "as-is":
- Type system (needs fractional-bit metadata)
- Numeric coercion/promotion rules
- Arithmetic semantics (`*`, `/`, `%`, shifts, comparisons) for fixed-point
- Backend lowering for fixed-point ops (rescaling and rounding)

### Hidden semantic decisions (high impact)
These are the real cost drivers:

1. **Signed `I` meaning**
- Does `I` include the sign bit, or count integer magnitude bits excluding sign?
- This must be fixed unambiguously in the spec.

Decision (from discussion, and consistent with native-width mapping):
- `#uI.F` / `#iI.F` total storage width is always `W = I + F`.
- `#iI.F` uses a **two's-complement signed integer of width `W`** as its raw representation.
- Therefore, for signed fixed-point, **`I` includes the sign bit**.

Reasoning:
- This preserves the requirement that when `W` is `8/16/32/64`, backends can map directly to `uintW_t` / `intW_t` storage and native integer operations.

2. **Overflow semantics**
- Wrap (integer-like)?
- Saturating?
- Configurable?

Recommendation for first version: **wrap**, to stay aligned with existing integer behavior.

3. **Rounding semantics for `*` and `/`**
- Truncate toward zero?
- Floor?
- Round-to-nearest-even?

Recommendation for first version: **truncate toward zero** (simplest, deterministic, backend-friendly).

4. **Mixed-scale result typing / requirement tracking**
- For `#u8.8 + #u4.12`, what is result type?
  - This should be treated as a **constraint/requirement propagation problem**, not only a promotion table.
- Same for multiplication/division.
  - If the expression is underconstrained, emit an error instead of inventing a default.

This requires a precise result-type policy plus requirement tracking rules.

5. **Literal classification and exactness**
- If non-`e` literals are treated as fixed-point/exact literals, what is the frontend literal core representation?
- Decimal literals (e.g. `0.1`) are not generally exactly representable in binary fixed-point formats.
- `e` notation literals are floating literals and should be typed as `#f64` (not smallest-fitting), which leaves decimal non-`e` literals as a distinct category with their own exactness and contextual-conversion rules.

Recommendation:
- Represent non-`e` fractional literals as **exact values** in the frontend (e.g. exact rational or exact fixed-literal form), not immediately as `#f32/#f64`.
- Track the literal's **minimum required integer/fraction shape** as metadata (useful for diagnostics and contextual concretization), but keep it unconcretized until context/casts require a concrete type.
- Let contextual typing/casts decide whether conversion into `#uI.F/#iI.F` is exact, truncating, or invalid (per explicit cast rules).
- Keep `e` notation as the explicit floating-literal path, always producing `#f64`.
- Backend implication: backends should not need to handle exact rational literal representations; frontend should lower/concretize literals before backend handoff.

Recommended implementation shape:
- Use an **exact rational** representation internally for non-`e` decimal literals (plus original token text/metadata for diagnostics/printing fidelity).
- This keeps literal-only arithmetic exact and avoids prematurely privileging binary fixed-point or floating representations.

### Decisions / constraints (locked from discussion)
- `#uI.F` / `#iI.F` storage width is `W = I + F`.
- `#iI.F` uses two's-complement signed raw representation of width `W`; **`I` includes the sign bit**.
- Fixed-point overflow semantics (v1): **wrap** (integer-like).
- Fixed-point `*` and `/` rounding (v1): **truncate toward zero**.
- Fixed-point requirement tracking follows normal Vexel type-resolution rules: underconstrained expressions are errors and require explicit casts/annotations.
- Implicit conversion from non-`e` exact decimal literals into fixed-point is allowed only when the value is **exactly representable** in the target type.
- Inexact/truncating fixed-point conversion requires an **explicit cast**.
- `%` is in scope for v1 (RFC must define its semantics precisely).
- For `F != 0`, disallow bitwise/shift operators and their compound forms (`~ & | ^ << >>` and `&= |= ^= <<= >>=`).
- `F < 0` is **in scope for v1** (with validity constraint `I + F > 0`).
- Numeric casts and bit reinterpretation must be distinct concepts; do not silently use a numeric cast as a bit reinterpretation.
- Non-`e` numeric literals are treated as exact fixed-point literals (arbitrary precision / exact frontend representation), carrying minimum-shape metadata but not forcing immediate concretization.
- `e`-notation literals are floating literals and should always be `#f64`.

### Recommendation on bit reinterpretation (specific)
Do **not** add a new cast operator or overload numeric casts for bit reinterpretation in v1.

Recommended path:
- Keep `->` as **numeric conversion only**.
- If bit reinterpretation is needed, introduce it later as an explicit **standard-library API surface** (e.g. under `std::bits`), with compiler/backend recognition as needed and strict preconditions:
  - source and destination total bit widths must match
  - no value conversion semantics (pure bit reinterpretation)
  - backend/layout constraints must be documented
- Suggested v1 scope for `std::bits` reinterpretation:
  - scalar numeric types only (`#uN/#iN`, fixed-point with equal total width, `#f32 <-> #u32`, `#f64 <-> #u64`)
  - exclude structs/arrays/bitfields/composites until layout/packing rules are specified independently
- This keeps the language/core cast semantics clean and avoids ambiguity while fixed-point semantics are still being formalized.

### Additional fixed-point semantic proposals (accepted direction)
#### `%` operator definition (proposed RFC wording direction)
- Define fixed-point remainder numerically from truncating division:
  - `a % b = a - trunc_to_zero(a / b) * b`
- Sign behavior should match Vexel integer `%` semantics.
- Result typing follows the same requirement-tracking rules as other fixed-point operators:
  - same concrete operand type -> same result type
  - mixed/underconstrained -> require contextual target type or explicit cast

#### `F < 0` interpretation (conceptually same model as `F > 0`)
- Use the same raw-value model for all fixed-point:
  - `value = raw * 2^-F`
- Therefore, `F < 0` simply means coarser quantization steps (e.g., multiples of powers of two), not a different numeric family.
- Casts, comparisons, and arithmetic should follow the same scaling/alignment semantics as any other fixed-point type.

### Cost analysis
#### Frontend
- **Type representation / parser / printing / hashing / generic mangling**: High
- **Typechecker numeric rules + inference/casts/promotions**: High
- **CTE exact fixed-point evaluation**: High
- **RFC/spec work**: High (semantic choices are the feature)

#### Backends (C + megalinker)
- **Native widths (8/16/32/64) fast path**: Medium
- **Arbitrary-width fixed-point helpers over extint runtime**: High
- **Duplication across backends (intentional)**: High maintenance cost

#### Tests/docs/examples
- **Very high** if done correctly (needs semantic edge coverage)

**Overall cost**: **High to Very High**

### Practical interpretation
This is not "an extension annotation" operationally; it is a **new numeric family** built on top of integer representation.

That is still a good design direction, but it should be treated as a major feature.

### Architectural recommendation
Do it only after writing a precise spec for:
- requirement tracking / result-typing rules for underconstrained fixed-point expressions
- overflow rules
- rounding rules
- comparison/cast semantics
- `%` semantics
- literal classification/conversion rules (non-`e` exact fixed literals vs `e` floating literals)
- whether/when bit reinterpretation exists (and if so, via which explicit API)

Otherwise it will sprawl into ad-hoc fixes quickly.

### Suggested scope for a coherent first version
- Types: `#uI.F`, `#iI.F`
- Conversions: fixed <-> int, fixed <-> fixed, fixed <-> `#f32/#f64`
- Operators:
  - `+ - * / %`
  - comparisons
  - unary `-`
  - casts
- No bitwise/shift operators when `F != 0` in v1
- No transcendental math yet (can come via math feature / library path)

### Risk summary
Representation reuse: **easy**
Semantics design + correctness: **hard**

---

## 3) Standard library RFC + `std/` repository folder

### Goal
Create a formal RFC/spec for the standard library surface (starting with `std::math`) and add a `std/` folder to the repo containing Vexel-defined standard modules.

### Feasibility
**Feasible**, and architecturally a good move.

This should come **before** compiler-recognized math implementation, even if it starts as a small placeholder:
- it defines the public surface (`std::math`) the frontend may later recognize for CTE folding
- it provides a stable module namespace and repository location
- it separates language RFC from standard-library RFC/spec

### Proposed behavior (from discussion)
- The repo contains `std/` with Vexel source modules, starting with `std/math` (placeholder is acceptable initially).
- The compiler treats internal `std/` as always available.
- If a project-local `std/` folder exists, it **overrides** the internal one (module-path based override; local project wins for matching modules).
- Resolution rule (accepted):
  1. resolve `std::X` from project-local `std/X` if present
  2. otherwise fall back to bundled internal `std/X`
- No symbol-level merging; override unit is the module path.
- Imports inside `std/` follow the same per-module fallback rule.

### Cost analysis
- **Compiler/module loader changes** (built-in std root + override resolution): Medium
- **RFC/spec work for std + std::math v1**: Medium to High
- **Initial std module authoring (math placeholder + later real surface)**: Medium
- **Tests/examples/docs**: Medium

**Overall cost**: **Medium**

### Notes from current repo (`examples/lib` placeholder)
The existing `examples/lib/` folder is a useful prototype/workbench, not a real standard library yet.

What is reusable conceptually:
- organization style (`math`, `print`, containers, etc.)
- example-facing module usage patterns

What should not be copied blindly:
- informal naming/surface without a spec
- modules that imply runtime guarantees before backend/frontend contracts are explicit

Recommendation:
- Start `std/` with a tightly specified `std/math` placeholder + RFC.
- Migrate/borrow from `examples/lib/` selectively after API review.
- Expand `std/` only after `std::math` behavior is stable.

---

## 4) Compiler-recognized math (cmath-like), with CTE support and backend-controlled runtime lowering

### Goal (as understood)
- Make math functionality (at least `f32`/`f64`) efficiently available.
- Let frontend evaluate math at compile time (for table generation, etc.).
- Let backends decide runtime implementation strategy.
- Prefer not to hardwire named constructs into the language core.
- Likely start with a library interface, then add shortcuts later if stable.

### Feasibility
**Feasible**, and the architecture can support it cleanly if done as:
- **library-facing API + frontend intrinsic recognition**
- not as language keywords

This is a good fit for Vexel’s model.

Dependency note:
- This should build on the `std/` feature above (at least a placeholder/spec for `std::math`), so compiler recognition targets a stable library surface.

### Best architectural direction (recommended)
#### Phase 1 (library-first, compiler-recognized)
Define a library/module interface (e.g. `math`) with normal Vexel declarations, and have the frontend optionally recognize those declarations/signatures as intrinsics for CTE.

Key point:
- The source code still uses normal function calls.
- Frontend CTE can evaluate recognized ones when args are compile-time known.
- Backends can either:
  - emit target math calls,
  - use target intrinsics,
  - use lookup tables / polynomial approximations,
  - or reject unsupported runtime cases.

This preserves language cleanliness.

#### Phase 2 (optional shortcuts, separate feature)
Only after semantics are stable, add shorthand syntax.

Locked direction:
- This is a **separate feature** to be done after the library-first/compiler-recognized path is stable.

### `cmath` vs alternatives (frontend implementation side)
For frontend CTE, `std::cmath` is still the pragmatic baseline.

Alternatives / complements:
- **`std::numbers`**: useful for constants (C++20), not a replacement for math functions.
- **openlibm**: more consistent libm behavior across platforms; good if determinism becomes a priority.
- **SLEEF / vector libs**: runtime optimization libraries, less relevant to frontend CTE.
- **CRLIBM / correctly-rounded math libs**: excellent for determinism/correct rounding, higher integration cost.

Recommendation:
- Start with **`std::cmath`** for feasibility.
- But define Vexel-level behavior explicitly where needed (NaN/Inf propagation, domain errors, etc.).

Refinement from discussion:
- Math should be **non-trapping** by default (no domain/range trapping behavior).
- Favor IEEE/cmath-style result propagation (NaN/Inf where applicable) over frontend exceptions for math domain errors.

### The real challenge (not parsing/codegen)
The hard part is **semantic specification and portability**, not function wrappers.

Mostly settled from discussion (remaining work is RFC wording + tests):
- NaN behavior expectations
  - Keep IEEE behavior; do not invent Vexel-specific NaN semantics.
  - `isnan(...)` remains useful and should be part of the API.
- domain/range error behavior (error vs NaN/Inf result)
  - **No trapping math** (frontend CTE and runtime expectations should align on this policy).
- exactness expectations for CTE vs runtime
  - Frontend CTE should target `cmath`/IEEE behavior for `#f32/#f64`.
- whether CTE results must be bit-identical across host compilers/platforms
  - Frontend should be specified as `cmath`-compatible, but strict cross-platform bit-identical guarantees may be too strong unless a stricter math backend/library is chosen later.
- std-array behavior staging
  - broadcasting is desired, but should be added after the same-shape array semantics land and are tested.

### Tightened math semantics proposal (for future RFC/std-math spec)
This should be explicit from the start to avoid flaky tests:

- **Domain/range behavior**: non-trapping; return IEEE/cmath results (NaN/Inf where applicable)
- **Supported scalar types (v1)**: `#f32`, `#f64` only
- **CTE behavior**: frontend evaluates recognized math calls using `std::cmath` semantics (document any host-lib variation risks); this is the semantic baseline
- **Runtime behavior (Option A)**: ordinary library calls unless backend chooses target-specific lowering; backend simplifications are allowed only if they stay within documented behavior/accuracy expectations for the selected target mode
- **Conformance testing**:
  - exact equality only where stable/obvious
  - tolerance-based checks for many transcendental functions
  - explicit NaN/Inf classification tests using `isnan/isinf/isfinite`
- **NaN usability**:
  - keep `isnan(...)` in the API
  - `x != x` remains a valid NaN detection idiom (no special Vexel override of IEEE behavior)
- **Non-trapping math policy**:
  - Vexel math should follow non-trapping `cmath`-style behavior (NaN/Inf propagation where applicable), not frontend traps/exceptions

### Backend contract implications
You are right that this spills into the backend contract.

There are two clean options:

#### Option A — No new backend contract node (simpler start)
- Keep calls as normal calls.
- Frontend folds recognized math calls when constexpr.
- Runtime path stays as library calls (externs/wrappers) unless backend chooses to special-case by annotation/symbol.

Pros:
- Minimal contract churn.
- Faster to adopt.

Cons:
- Backend intrinsic selection is less explicit / more symbol-name-dependent.

#### Option B — Explicit intrinsic markers in analyzed handoff (cleaner long-term)
- Frontend recognizes math calls and annotates them as intrinsic operations in the analyzed contract.
- Backends receive explicit semantic intent.

Pros:
- Cleaner backend decision-making.
- Better long-term for optimization/target specialization.

Cons:
- Requires contract extension and backend updates.

Given your current trajectory, **Option A first** is the chosen tradeoff.

### Cost analysis
#### Library-first + frontend CTE recognition (core subset)
- **Frontend typechecker signature recognition / validation**: Medium
- **CTE evaluator math implementations (f32/f64)**: Medium to High
- **Backend changes (none to minimal if ordinary calls are preserved)**: Low to Medium
- **Tests/spec/docs**: High (numerics need strong coverage)

**Overall cost**: **Medium to High**

#### Full cmath parity + explicit backend intrinsic contract
- **Frontend + contract + backends + tests**: High to Very High

### Recommended initial scope
Core numeric functions only (example set):
- `sqrt`, `sin`, `cos`, `tan`
- `asin`, `acos`, `atan`, `atan2`
- `exp`, `log`, `log2`, `log10`
- `pow`
- `floor`, `ceil`, `trunc`, `round`
- `fabs`, `fmod`
- classification helpers: `isnan`, `isinf`, `isfinite` (if the language can express them cleanly)
  - `isnan` should definitely be kept.

Array support extension (requested):
- Where mathematically sensible, `std::math` functions should support **array inputs** and return the **same shape** (element-wise mapping).
- This should be part of the definition, not an afterthought.
- Recommendation: define this as a library/frontend rule layered on top of the same function names (`sin(array)` maps per-element), with clear shape semantics and explicit errors on unsupported shapes/mixed shapes.

Staged array behavior (accepted proposal):
- **Phase 1** (same-shape arrays only):
  - unary functions: scalar -> scalar, array -> same-shape array
  - binary functions: scalar/scalar and exact-shape array/array only
  - reject scalar/array and array/scalar in this phase
- **Phase 2** (broadcasting):
  - binary functions adopt the same broadcasting rules as per-element operators
  - broadcasting rules are frontend-defined and compile-time checked (trailing-dimension alignment, `1` expansion, rank-0 scalar lifting)

Start with:
- `#f32`, `#f64`
- CTE folding when args are constexpr
- ordinary runtime calls via library declarations
- No arbitrary-width integer/fixed-point integration in std-math v1 (math library operates on `#f32/#f64` only)
- `e` literals feeding math are `#f64` by default (per literal policy), with explicit casts used when `#f32` is desired.

### Interaction with targets without floating-point hardware
This is actually a strong use case for frontend CTE:
- Generate tables at compile time
- Fold large parts of numeric prep work
- Backend can still emit integer tables for non-FP targets

Runtime floating-point execution on such targets remains a backend policy question (soft-float, library, or rejection).

### Architectural recommendation
Do this as a **library + optional compiler recognition** feature, not a language-core keyword feature.

That aligns with your design goals and minimizes language pollution.

---

## 5) Native vector/matrix language support (independent feature)

### Goal (as understood)
Add native vector and matrix types as first-class language constructs with explicit operator semantics (including interaction with scalar operators and per-element operators).

### Feasibility
**Feasible**, but materially larger than per-element overload syntax.

Why:
- It is not only syntax; it introduces type/shape semantics the frontend must understand.
- CTE must understand value representation and operations on vectors/matrices.
- Backends need lowering rules for native vector/matrix values (even if some paths lower to structs/arrays).

### High-impact design decisions (before implementation)
- Shape encoding in the type system (dimensions as compile-time constants, generic dimensions, or both)
- Storage/layout guarantees (row-major, contiguous, alignment rules)
- Operator semantics split:
  - scalar ops
  - matrix multiplication
  - per-element ops
- Interop with arrays / tuples / named structs
- Whether vector/matrix literals are new syntax or sugar over existing array/named constructs

### Cost analysis
- Frontend type system + checker: **High**
- CTE support: **High**
- Backend lowering (C + megalinker): **Medium to High**
- RFC/spec/tests/examples: **High**

**Overall cost**: **High**

---

## Cross-feature comparison (feasibility / cost)

### Best cost/benefit first
1. **Per-element operators (staged: same-shape first, then broadcasting)** — best near-term value; moderate for 1A, high for full feature
2. **Standard library RFC + `std/` folder (with `std/math` placeholder)** — high leverage, medium cost, enables clean math integration
3. **Compiler-recognized math via `std::math` (library-first + frontend CTE recognition, Option A)** — high value, moderate/high cost, strong payoff
4. **Fixed-point numeric family** — very valuable, but highest semantic cost
5. **Native vector/matrix language support** — high value, but large semantics/CTE/backend impact (best after per-element semantics/broadcasting are stable)

### Why fixed-point should probably come after math (despite integer reuse)
- Fixed-point requires a large numeric semantics spec (promotion/rounding/overflow).
- Math can start on existing `#f32/#f64` with a library-first interface and immediately improves CTE usefulness.

### If you want maximum long-term coherence
Do a numeric-spec pass first (brief but exact), covering:
- overflow conventions (integers/fixed)
- rounding conventions (fixed/math casts)
- NaN/Inf policy for math CTE
- backend obligations vs optional support
Decision locked from discussion:
- `cmath` is the baseline for frontend std-math behavior.

That would de-risk both fixed-point and compiler-recognized math.

## Summary (short)

- **Per-element operators (staged: same-shape first, then broadcasting)**: **Feasible**, strong next step; complete operator set is in scope, with cost rising from medium (1A) to high (with broadcasting).
- **Standard library RFC + `std/` folder**: **Feasible, medium cost**, good foundation for `std::math` and future standard modules.
- **Compiler-recognized math via `std::math` (library-first, Option A)**: **Feasible, medium/high cost**, strong fit for CTE and backend flexibility; `cmath` baseline, non-trapping, `#f32/#f64` only in v1, with element-wise array support where defined.
- **Fixed-point `#uI.F/#iI.F`**: **Feasible, high/very-high cost**, representation reuse is easy, semantics + requirement tracking are the hard part.
- **Native vector/matrix types**: **Feasible, high cost**, independent feature (not a sub-step of per-element operators), best after per-element semantics/broadcasting stabilize.
