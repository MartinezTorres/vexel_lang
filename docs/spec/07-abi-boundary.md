# 7. ABI Boundary and Interop

RFC references: [Declarations](../vexel-rfc.md#declarations), [Annotations & Lowered Form](../vexel-rfc.md#annotations--lowered-form)

## 7.1 ABI Visibility Markers

### Functions

- exported function: `&^name(...)`
- external function import: `&!name(...)`
- internal function: `&name(...)`

### Globals

- exported global: `^name:#T = ...;`
- external symbol global: `!name:#T;`
- backend-bound global: `!!name:#T;`

## 7.2 Exported Global Constraints

- exported globals are ABI-visible roots,
- exported global initialization must satisfy frontend compile-time constraints,
- shape/layout constraints are part of ABI surface semantics.

## 7.3 Named Struct ABI Use

Named struct types may cross ABI boundaries where backend contract supports that surface (for example exported globals with struct element/array composition).

Backends may reject unsupported ABI forms with explicit diagnostics.

## 7.4 Backend-Bound Globals (`!!`)

`!!` denotes declarations whose binding details are backend-defined (e.g., address/banking/target-specific placement semantics).

Frontend treats them as declarations with preserved annotations and type semantics; backend enforces binding completeness.

## 7.5 Annotation Ownership

Language semantics:

- frontend parses and preserves annotations,
- frontend does not enforce global backend-annotation whitelist,
- backend validates backend-owned annotations.

Currently documented backend-facing examples include:

- `[[nonreentrant]]` (backend-consumed)
- `[[sdcccall(n)]]` (backend-specific ABI control)
- `[[nonbanked]]` (megalinker backend specific)

## 7.6 Unknown Backend Options and Flags

Unified driver behavior:

- frontend consumes known frontend options,
- unknown options are forwarded to selected backend,
- if neither recognizes them, compilation fails with combined usage.

## 7.7 Entry/Exit Boundary Defaults

Reentrancy defaults and ABI conventions are backend-owned and documented per backend README.

Language-level semantics remain frontend-owned and backend-agnostic.

## 7.8 Dead ABI Surface Pruning Rule

Frontend DCE must preserve ABI-visible roots and any declarations reachable from them.

Non-visible, unreachable declarations must be pruned before backend emission.
