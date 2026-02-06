# Frontend Pass Contracts

This document defines the expected pre/post conditions for each frontend pass.
It is intentionally strict: if a pass violates one of these contracts, that is
an internal compiler bug.

## Pipeline Order

1. Module load (`ModuleLoader`)
2. Symbol and scope resolution (`Resolver`)
3. Type checking (`TypeChecker`)
4. Monomorphization (`Monomorphizer`)
5. Lowering (`Lowerer`)
6. Constexpr discovery (`Optimizer`)
7. Reachability/effects/reentrancy/ref analysis (`Analyzer`)
8. Concrete-type use validation (`TypeUseValidator`)
9. Backend emission

## Global Invariants

- AST nodes are never null in `Module::top_level`.
- `Expr::Kind::Iteration` stores iterable in `operand` and body in `right`.
- `Expr::Kind::Repeat` stores condition in `condition` and body in `right`.
- Loop nodes do not use `left`.
- Import/type/function names are non-empty when declared.

## Pass Contracts

### ModuleLoader

- Pre:
  - Entry path resolves to a readable module file.
- Post:
  - All import dependencies are loaded into `Program::modules`.
  - `Program::path_to_id` maps normalized module paths.

### Resolver

- Pre:
  - Program modules are parsed and structurally valid.
- Post:
  - Instance scopes exist for all instantiated modules.
  - Declarations are bound in `Bindings`.
  - Assignment declarations are marked as new-variable assignments when applicable.

### TypeChecker

- Pre:
  - Resolver has populated declaration/binding relationships.
- Post:
  - Value-producing expressions have `expr->type`.
  - Function/type/variable declarations are type-checked.
  - TypeVars may remain unresolved only when later usage permits.

### Monomorphizer

- Pre:
  - Generic call sites and instantiation requests are known.
- Post:
  - Concrete instantiations are materialized in the lowered module.
  - Generic templates are not emitted directly.

### Lowerer

- Pre:
  - Type-checker invariants hold.
- Post:
  - Syntax is normalized for backend-friendly emission.
  - Existing inferred types are preserved.

### Optimizer

- Pre:
  - Lowered module is structurally/type valid.
- Post:
  - `OptimizationFacts` contains constexpr expressions/conditions.
  - Foldable function set and fold-skip reason categories are populated.

### Analyzer

- Pre:
  - Optimization facts available.
- Post:
  - Reachable function set is complete.
  - Reentrancy variants and ref variants are derived.
  - Effects and mutability maps are populated for reachable code.

### TypeUseValidator

- Pre:
  - Analyzer facts are available.
- Post:
  - Any expression value used in a concrete context has concrete type.
  - Compile-time-dead branches are excluded from concrete-type requirements.

## Debug Invariant Checks

Debug builds can enforce pass invariants after each stage.

- Frontend target: `make frontend-debug-invariants-test`
- Mechanism: builds with `-DVEXEL_DEBUG_PASS_INVARIANTS` and runs frontend tests.
- Behavior: compiler throws internal invariant failures with stage labels.

