# 6. Modules, Resources, and `std` Resolution

RFC references: [Name Resolution & Modules](../vexel-rfc.md#name-resolution--modules), [Annotations & Lowered Form](../vexel-rfc.md#annotations--lowered-form)

## 6.1 Module Imports

Import statement syntax:

```vx
::lib::math;
```

Rules:

- imports are explicit and lexical,
- imported names participate in resolver scope rules,
- no implicit global module injection.

## 6.2 Module Search Behavior

Resolution is whole-program aware.

Standard fallback behavior:

- `::std::*` first checks project-local module path context,
- if absent, falls back to bundled `std/` module implementation.

## 6.3 Resource Expressions

Expression-context resource syntax:

```vx
bytes = ::assets::sprite.bmp;
```

Semantics:

- resource is loaded at compile time,
- value is immutable,
- mutation attempts are compile-time errors.

## 6.4 Local Overrides and Isolation

Per-module overrides are supported through normal module resolution rules.

One module's local override must not implicitly mutate another module's import resolution unless that module resolves through the same path.

## 6.5 Process Module

`::process` features are explicit and gated.

Compilation requires opt-in (`--allow-process`) when process execution is present.

## 6.6 Namespaces and Qualified Access

Type and function namespaces are distinct in `#Type(...)` vs value/function calls.

Qualified names are resolved through module/scope graph; ambiguity results in deterministic diagnostics.

## 6.7 Import Semantics and CTE

Imported pure code participates in CTE and DCE like local code.

No special runtime-only exemption exists for imported code.

## 6.8 Stability Requirement

Module/resource semantics belong to frontend contract.

Backends consume already-resolved module graph and must not re-resolve module/resource semantics.
