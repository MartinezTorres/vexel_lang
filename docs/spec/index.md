# Vexel Detailed Specification (v1.0-rc1)

**Role**: Specification (detailed semantics and elaboration of the RFC).

This document set expands `docs/vexel-rfc.md` into an implementation-focused, readable specification with examples.

Normative order:

1. `docs/vexel-rfc.md` (language source of truth)
2. `docs/spec/*` (detailed elaboration of RFC rules)
3. frontend/backend code comments and contracts

When this detailed spec and the RFC diverge, the RFC wins and this spec must be updated.

## Chapter Map

1. [Lexical and Tokens](01-lexical.md)
2. [Grammar and Parsing Model](02-grammar.md)
3. [Type System](03-types.md)
4. [Execution Model](04-execution-model.md)
5. [Analysis and Optimization Model](05-analysis-optimization.md)
6. [Modules, Resources, and `std` Resolution](06-modules-resources-std.md)
7. [ABI Boundary and Interop](07-abi-boundary.md)
8. [Diagnostics and Strictness](08-diagnostics-strictness.md)
9. [Frontend/Backend Contract](09-backend-contract.md)
10. [Conformance and Release Gates](10-conformance.md)
11. [Bundled Standard Library Semantics (`std`)](11-std-bundled.md)

## Scope

This spec set is exhaustive for v1.0-rc1 language behavior and operational contracts needed to maintain compiler consistency.

It deliberately includes:

- parsing disambiguation rules,
- type and representability constraints,
- compile-time execution/dead-code expectations,
- backend boundary guarantees,
- conformance expectations for tests and CI.

## RFC Cross-Reference

Primary RFC sections used by these chapters:

- [Preamble](../vexel-rfc.md#preamble)
- [Lexical & Tokens](../vexel-rfc.md#lexical--tokens)
- [Types](../vexel-rfc.md#types)
- [Declarations](../vexel-rfc.md#declarations)
- [Expressions & Control](../vexel-rfc.md#expressions--control)
- [Annotations & Lowered Form](../vexel-rfc.md#annotations--lowered-form)
- [Name Resolution & Modules](../vexel-rfc.md#name-resolution--modules)
- [Type Inference & Generics](../vexel-rfc.md#type-inference--generics)
- [Runtime Semantics](../vexel-rfc.md#runtime-semantics)
- [Operations](../vexel-rfc.md#operations)
- [Errors & Diagnostics](../vexel-rfc.md#errors--diagnostics)
- [Grammar](../vexel-rfc.md#grammar)
