# 8. Diagnostics and Strictness

RFC references: [Errors & Diagnostics](../vexel-rfc.md#errors--diagnostics), [Type Inference & Generics](../vexel-rfc.md#type-inference--generics)

## 8.1 Diagnostic Principles

Diagnostics must be:

- deterministic,
- explicit on contract violations,
- free of silent semantic fallback.

## 8.2 Parse Diagnostics

Parser reports structural violations such as:

- malformed declarations,
- invalid control token forms,
- ambiguous/invalid syntax not resolvable by grammar rules.

Examples:

- missing `:` in typed declaration,
- malformed constructor/type declaration syntax,
- invalid loop control spacing where a control token is required.

## 8.3 Type Diagnostics

Typechecker reports:

- unresolved required concrete value uses,
- representability errors,
- invalid cast/operation combinations,
- generic inference conflicts.

## 8.4 Reachability-Aware Type Validation

Concrete-type requirements are enforced for reachable/used value paths.

Dead branches and unused return chains are handled by frontend analysis contracts before final value-use validation.

## 8.5 Strictness Levels

Driver flag `--type-strictness`:

- `0`: relaxed unresolved integer flow where legal by inference contracts.
- `1`: requires explicit type annotations for new variable declarations.
- `2`: rejects unresolved literal flow across inferred call boundaries unless explicitly constrained.

Alias:

- `--strict-types=full` is equivalent to `--type-strictness=2`.

## 8.6 Arithmetic/Operator Diagnostics

Compiler reports explicit errors for:

- invalid operand domains,
- unsupported fixed-point operation forms,
- division/modulo by zero in compile-time-known contexts,
- shape mismatch in per-element operations.

## 8.7 Module/Resource Diagnostics

Compiler reports:

- unresolved module/resource paths,
- invalid process import usage when process mode disabled,
- invalid local override shapes that violate module resolution contracts.

## 8.8 Backend Diagnostic Boundary

Backend-specific diagnostics cover target-specific constraints (ABI/layout/codegen support).

Frontend diagnostics cover language semantics and graph-level guarantees.
