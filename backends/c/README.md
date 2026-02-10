# Vexel Backend Specification – C (v0.2.1)

This backend emits portable C as the reference backend. It consumes the lowered, monomorphized, annotation-preserving frontend output.

Source-of-truth integration points:

- Frontend lowering contract: `frontend/src/transform/lowerer.h`
- Frontend pipeline order: `frontend/src/cli/compiler.cpp`
- Backend plugin API: `frontend/src/support/backend_registry.h`

## Scope & Versioning
- Targets language semantics in `vexel-rfc.md` v0.2.1.
- Backend revision v0.2.1 tracks the language version; backend-only fixes should bump the patch component and be recorded here.

## Shared Front-End Behavior
- Compile-time execution, constant folding, and purity analysis are completed before this backend runs.
- Imports/resources/process expressions are resolved; only literal data reaches codegen.
- Iteration operands evaluate once; `@@` emits a sorted copy via generated comparator helpers (lexicographic ordering for all types).
- Diagnostics from parsing/type checking are backend-agnostic; this backend emits only C-emission errors.

## Target & ABI
- Target: portable C output for host C11 toolchains.
- Integer widths in generated code: `i8/u8 → int8_t/uint8_t`, `i16/u16 → int16_t/uint16_t`, `i32/u32 → int32_t/uint32_t`, `i64/u64 → int64_t/uint64_t`.
- Floats: `f32 → float`, `f64 → double`.
- Bool: `_Bool`.
- Pointers: emitted as native C pointers for the host toolchain.

## Name Mangling & Identifiers
- Mangled identifiers use the `__` prefix defined in `constants.h` (`MANGLED_PREFIX`).
- Tuples: named `__tuple<N>_<type...>` where `<type>` components use primitive/type names (e.g., `__tuple2_i32_u8`).
- Generated temporaries and helper functions share the mangling prefix and remain `static` unless exported.
- Receiver functions lower to `<Type>__method` forms; mutating receivers lower to pointer parameters, non-mutating receivers lower by value.

## Types & Layout
- Structs map directly to C `struct` with field order preserved. No padding adjustments beyond C defaults; alignment follows host compiler rules.
- Tuples map to generated `struct` types with fields `__0`, `__1`, … in declaration order.
- Arrays map to C arrays with compile-time extent. No VLAs are emitted.
- Strings emit as `const char[]` with explicit byte length; runtime code must treat them as immutable.

## Functions & Calling
- Direct Vexel calls become direct C calls; tail-call optimisation is optional.
- Receiver/multi-receiver methods: mutating receivers are pointers, non-mutating receivers are values; rvalue receivers for mutating methods are materialized into temporaries; expression parameters are fully specialized before codegen.
- When both mutable and non-mutable receiver paths are used, the backend emits specialized C functions per receiver mutability mask (suffix `__ref<mask>`, `M` = mutable reference, `N` = non-mutable/value).
- When both reentrant and non-reentrant call paths reach a function, the backend emits two variants (`__reent` and `__nonreent`) and call sites select the appropriate variant.
- Exported (`&^`) functions are non-`static` and declared in the header; internal functions are `static`.
- External (`&!`) declarations emit as `extern` prototypes only.
- Function declarations/definitions are preceded by `// VEXEL: ...` line comments carrying backend-visible traits (`reentrant`, receiver ref-mask, export, inline/noinline, purity, no-global-write, ABI shape).

## Globals & Data
- Immutable globals emit as `const` in the `.c` file; mutable globals emit as `static`.
- Strings and read-only data are placed in `.rodata` via `const`.
- No per-backend runtime state beyond standard C.
- The C output annotates variables with `VX_MUTABLE`, `VX_NON_MUTABLE`, and `VX_CONSTEXPR` for visibility. Defaults live in the generated header (`VX_MUTABLE` empty, others `const`) and can be overridden before inclusion.

## File Structure
- Exactly one `.c` and one `.h` file per compilation unit.
  - Default names: `<module>.c` / `<module>.h`; overrides via `-o` adjust the stem.
  - `.h` declares exported and external functions and any exported types.
  - `.c` includes the header and defines all functions, globals, tuple types, and comparator helpers.

## Toolchain Assumptions
- Host compiler: `gcc -std=c11` (or compatible). Optimisation level is left to the caller (examples use `-O2`).
- No inline assembly is generated. Any future assembly must be guarded to keep other backends unaffected.

## Diagnostics Specific to This Backend
- Emit errors when C emission would be invalid (e.g., pass-by-value of zero-sized struct).
- Warn if integer widths exceed host platform limits.

## Testing Notes
- Default test mode targets this backend. Regression tests exercise emitted C structure (single source/header, mappings above) and linkage via host gcc.
