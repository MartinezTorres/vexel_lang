# Vexel Backend Specification – Banked Architecture (v0.2.1)

> **Implementation status (experimental)**: The compiler emits pageA/pageB entry points, alternation analysis for non-reentrant functions, per-symbol banked outputs, Megalinker macro calls, and basic SCC-based co-location via `ML_MOVE_SYMBOLS_TO`. A matching `megalinker.h` ships under `backends/banked/include/`; more advanced co-location policies remain future work.
>
> **Annotations**: `[[reentrant]]` on a function signals it may share state safely; the banked backend requires `[[reentrant]]` on any recursive function, omits page save/restore for reentrant wrappers, and uses `[[reentrant]]` to break alternation conflicts in the call graph. `[[nonbanked]]` on a global forces RAM placement even if immutable. Unknown annotations are preserved for future use.

## Scope & Versioning
- Implements the language semantics of `vexel-rfc.md` (v0.2.1).
- Backend revision `v0.2.1` tracks the language version; backend-only fixes should update this document while keeping the major.minor pair aligned.

## Shared Behavior
- Compile-time execution, purity checks, monomorphisation, and other frontend passes are identical to the portable C backend.
- Optimisation and IR construction remain backend-neutral; divergence happens only during final code emission.
- Diagnostics emitted before code generation are shared. This backend raises additional errors exclusively for banked-architecture restrictions.
- Iteration operands evaluate exactly once. `@@` loops allocate a temporary buffer in the current bank (demoted to `static` storage for non-reentrant functions), reuse the shared comparator helpers, and iterate over the sorted copy while leaving the original data untouched.
- Resource expressions and process imports are resolved before code generation; this backend receives only literal arrays/strings, so no additional linker steps are required.

## Output Layout
- Generates a single public header `banked/<module>.h`.
  - Declares every exported (`&^`) function twice: `foo_pageA` and `foo_pageB`.
  - Declares inline helpers and bank-switch macros used by generated sources.
  - Defines ROM-pointer types that pair a page token with an address.
- Emits one `.c` file per ROM-resident symbol:
  - For each function `foo`: `banked/foo.pageA.c` and `banked/foo.pageB.c`.
  - For each immutable global `BAR`: `banked/BAR.rom.c`.
- Emits a single `banked/ram.c` that aggregates mutable globals (optionally honouring user-specified addresses via SDCC `__at` attributes).
- Runtime helpers shared across compilations (e.g., the implementation of `load_page`) reside outside generated code and are provided by the support library.

## Module Naming & Megalinker Semantics
- Each generated `.c` file forms a distinct SDCC module. Filenames double as module names to align with Megalinker expectations.
- All ROM modules are marked `__banked`; RAM support files are `__nonbanked`.
- The backend emits Megalinker hook macros for every module:
  - `ML_REQUEST_A(foo_pageA_module)` precedes any use.
  - `ML_SEGMENT_A(foo_pageA_module)` materialises the page identifier.
  - `ML_LOAD_MODULE_A(foo_pageA_module)` / `ML_RESTORE_A(previous)` wrap calls (macro expansion mirrors `ML_EXECUTE_A` usage documented by Megalinker).
- When several symbols must reside together (e.g., mutually-recursive helpers), the backend emits matching `ML_MOVE_SYMBOLS_TO` directives to co-locate the modules in a single segment.

## Bank Switching & Calling Convention
- Every Vexel function produces two callable entry points: `foo_pageA` and `foo_pageB`.
  - Prior to invoking `foo_pageX`, callers emit:
    ```
    ML_REQUEST_A(foo_pageX_module);
    uint8_t _prev = ML_LOAD_MODULE_A(foo_pageX_module);
    /* call */
    ML_RESTORE_A(_prev);
    ```
    (The backend writes this pattern; the runtime may inline it via `ML_EXECUTE_A`.)
  - `foo_pageA` may only call `_pageB` variants of other functions; `_pageB` may only call `_pageA`. This enforces alternation across pages; additional pages (C/D) may be introduced later but are currently unused.
- Calls issued from `_pageB` bodies use the analogous `ML_REQUEST_B` / `ML_LOAD_MODULE_B` / `ML_RESTORE_B` sequence.
- Entry-point exports exposed to external code are `_pageA` versions; `_pageB` forms are internal to satisfy the alternation rule.
- Receiver functions lower to C functions whose first parameter is a pointer to the receiver; only pointer arguments cross the ABI.

## Data Placement Rules
- Immutable globals (strings, lookup tables, etc.) each receive their own module and are tagged `__banked` so Megalinker treats them as ROM modules.
- Mutable globals live in `ram.c` and are tagged `__nonbanked`; the backend can assign fixed addresses when requested.
- ROM pointers become a struct:
  ```c
  typedef struct {
      uint8_t page;
      const void *addr;
  } rom_ptr_t;
  ```
  Page constants (e.g., `foo_page_symbol`) are emitted as `extern` references resolved by Megalinker.
- RAM pointers remain plain `T*` because RAM is always mapped.

## Calling Restrictions & Diagnostics
- Structs cannot be passed or returned by value. The backend rewrites calls to use out-parameters; if it cannot, it raises an error.
- The call graph is analysed for reentrancy. Entry-point exports are marked non-reentrant; their callees may use dedicated global scratch buffers. Functions proven reentrant must avoid shared mutable state.
- Floating-point operations must be fully resolved at compile time. Any residual runtime FP expression triggers a diagnostic.
- Recursion is allowed only if alternation constraints can be satisfied without violating module residency; otherwise compilation fails with an explanatory error.

## Integration Notes
- Generated sources target SDCC for MSX-style banked hardware and are intended to link with [Megalinker](https://github.com/MartinezTorres/megalinker/blob/master/README.md).
- Symbols exported from ROM modules are annotated so Megalinker can locate them (`__banked`, `.module` directives). Page tokens follow the `_ML_A_` naming convention.
- The runtime must provide:
  - Implementations of `ML_LOAD_MODULE_A/B`, `ML_RESTORE_A/B`, and friends.
  - `load_page` inline wrappers used in generated code.
  - Optional trampolines placed in `__nonbanked` memory for inter-module dispatch.

## Megalinker Interface Header
- Generated translation units include `#include <megalinker.h>`; the project ships `megalinker.h` under `backends/banked/include/` matching the interface below:
  ```c
  #pragma once
  #include <stdint.h>

  #define ML_MOVE_SYMBOLS_TO(target, source) const uint8_t __at 0x0000 __ML_MOVE_SYMBOLS_TO_##target##_FROM_##source

  #define ML_REQUEST_A(module) extern const uint8_t __ML_SEGMENT_A_##module
  #define ML_REQUEST_B(module) extern const uint8_t __ML_SEGMENT_B_##module
  #define ML_REQUEST_C(module) extern const uint8_t __ML_SEGMENT_C_##module
  #define ML_REQUEST_D(module) extern const uint8_t __ML_SEGMENT_D_##module

  #define ML_SEGMENT_A(module) ((const uint8_t)&__ML_SEGMENT_A_##module)
  #define ML_SEGMENT_B(module) ((const uint8_t)&__ML_SEGMENT_B_##module)
  #define ML_SEGMENT_C(module) ((const uint8_t)&__ML_SEGMENT_C_##module)
  #define ML_SEGMENT_D(module) ((const uint8_t)&__ML_SEGMENT_D_##module)

  #define ML_LOAD_SEGMENT_A(segment) __ML_LOAD_SEGMENT_A(segment);
  #define ML_LOAD_SEGMENT_B(segment) __ML_LOAD_SEGMENT_B(segment);
  #define ML_LOAD_SEGMENT_C(segment) __ML_LOAD_SEGMENT_C(segment);
  #define ML_LOAD_SEGMENT_D(segment) __ML_LOAD_SEGMENT_D(segment);

  #define ML_LOAD_MODULE_A(module) ML_LOAD_SEGMENT_A(ML_SEGMENT_A(module))
  #define ML_LOAD_MODULE_B(module) ML_LOAD_SEGMENT_B(ML_SEGMENT_B(module))
  #define ML_LOAD_MODULE_C(module) ML_LOAD_SEGMENT_C(ML_SEGMENT_C(module))  // C/D reserved; compiler currently emits only A/B
  #define ML_LOAD_MODULE_D(module) ML_LOAD_SEGMENT_D(ML_SEGMENT_D(module))

  #define ML_RESTORE_A(segment) __ML_RESTORE_A(segment);
  #define ML_RESTORE_B(segment) __ML_RESTORE_B(segment);
  #define ML_RESTORE_C(segment) __ML_RESTORE_C(segment);
  #define ML_RESTORE_D(segment) __ML_RESTORE_D(segment);

  #define ML_EXECUTE_A(module, code) do { ML_REQUEST_A(module); uint8_t _old = ML_LOAD_MODULE_A(module); { code; } ML_RESTORE_A(_old); } while (0)
  #define ML_EXECUTE_B(module, code) do { ML_REQUEST_B(module); uint8_t _old = ML_LOAD_MODULE_B(module); { code; } ML_RESTORE_B(_old); } while (0)
  #define ML_EXECUTE_C(module, code) do { ML_REQUEST_C(module); uint8_t _old = ML_LOAD_MODULE_C(module); { code; } ML_RESTORE_C(_old); } while (0)
  #define ML_EXECUTE_D(module, code) do { ML_REQUEST_D(module); uint8_t _old = ML_LOAD_MODULE_D(module); { code; } ML_RESTORE_D(_old); } while (0)

  #ifdef __SDCC
  inline uint8_t __ML_LOAD_SEGMENT_A(uint8_t segment) { extern volatile uint8_t __ML_current_segment_a, __ML_address_a; register uint8_t old = __ML_current_segment_a; __ML_address_a = __ML_current_segment_a = segment; return old; }
  inline uint8_t __ML_LOAD_SEGMENT_B(uint8_t segment) { extern volatile uint8_t __ML_current_segment_b, __ML_address_b; register uint8_t old = __ML_current_segment_b; __ML_address_b = __ML_current_segment_b = segment; return old; }
  inline uint8_t __ML_LOAD_SEGMENT_C(uint8_t segment) { extern volatile uint8_t __ML_current_segment_c, __ML_address_c; register uint8_t old = __ML_current_segment_c; __ML_address_c = __ML_current_segment_c = segment; return old; }
  inline uint8_t __ML_LOAD_SEGMENT_D(uint8_t segment) { extern volatile uint8_t __ML_current_segment_d, __ML_address_d; register uint8_t old = __ML_current_segment_d; __ML_address_d = __ML_current_segment_d = segment; return old; }

  inline void __ML_RESTORE_A(uint8_t segment) { extern volatile uint8_t __ML_current_segment_a, __ML_address_a; __ML_address_a = __ML_current_segment_a = segment; }
  inline void __ML_RESTORE_B(uint8_t segment) { extern volatile uint8_t __ML_current_segment_b, __ML_address_b; __ML_address_b = __ML_current_segment_b = segment; }
  inline void __ML_RESTORE_C(uint8_t segment) { extern volatile uint8_t __ML_current_segment_c, __ML_address_c; __ML_address_c = __ML_current_segment_c = segment; }
  inline void __ML_RESTORE_D(uint8_t segment) { extern volatile uint8_t __ML_current_segment_d, __ML_address_d; __ML_address_d = __ML_current_segment_d = segment; }
  #endif
  ```
- The actual header includes the full inline definitions provided by Megalinker; generated code relies solely on the macros above, preserving compatibility with upstream updates.
- When the backend co-locates symbols, it emits `ML_MOVE_SYMBOLS_TO` invocations so Megalinker merges the modules before placement.

## Testing Guidance
- Shares all frontend/unit tests with the C backend to ensure semantics match.
- Backend-specific regression tests should verify:
  - Alternating call discipline and emitted `ML_*` macros.
  - ROM pointer packaging and dereference helpers.
  - Compile-time rejection of floating-point operations and struct-by-value calls.
