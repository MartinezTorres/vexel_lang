# 4. Execution Model

RFC references: [Runtime Semantics](../vexel-rfc.md#runtime-semantics), [Expressions & Control](../vexel-rfc.md#expressions--control), [Preamble](../vexel-rfc.md#preamble)

## 4.1 Whole-Program Assumption

Vexel compilation assumes all source for the program graph is available at compile time.

Consequences:

- frontend can perform global analysis and deep compile-time execution,
- dead declarations can be removed before backend emission,
- ABI visibility is explicit through exports/imports.

## 4.2 Purity and Side Effects

Language model intentionally excludes pointers and heap allocation.

Mutable effects are explicit through assignments to variables/globals.

External calls and process expressions are explicit boundaries where compile-time purity may not hold.

## 4.3 Compile-Time Execution (CTE)

CTE is frontend-owned and first-class:

- evaluates constexpr-eligible paths and expressions,
- propagates facts to pruning/liveness/type-use validation,
- operates at sub-expression granularity, not only whole-function granularity.

Unknown compile-time facts must not be silently defaulted.

## 4.4 Dead-Code Elimination (DCE)

DCE is frontend-owned and includes:

- declaration-level pruning (unreachable functions/types/globals),
- statement/expression pruning under known compile-time conditions,
- unused pure-expression elimination where observably safe.

## 4.5 Control Semantics

- `-> expr;` returns a value from current function.
- `->;` returns without value.
- `->|;` exits innermost loop.
- `->>;` skips to next innermost loop iteration.

`&&` and `||` are short-circuiting logical operators.

Dotted logical operators are non-short-circuit method-style operators.

## 4.6 Global Initialization

Global initialization semantics are analyzed in frontend graph terms:

- compile-time-known initializers are folded,
- runtime-dependent initialization remains residualized,
- exported global constraints are validated at ABI boundary.

## 4.7 Optional Semantic Blocks

`#{...}` regions are parsed and may be semantically discarded if unresolved/non-applicable in optional context.

They are not a substitute for required language correctness on reachable mandatory paths.

## 4.8 Process Expressions

- Process execution is opt-in via `--allow-process`.
- Disabled by default for safety.
- Treated as explicit side-effect boundary.

## 4.9 No Implicit Runtime API

The language provides no implicit standard runtime surface.

Any I/O or platform interaction is explicit through imports/modules/backends.

## 4.10 Runtime Error Model

Language-level error handling constructs are absent.

When unrecoverable runtime errors occur in emitted target code, behavior follows backend/runtime model (typically trap/abort semantics).
