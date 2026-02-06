# Vexel Frontend

The frontend performs lexing, parsing, type checking, constant evaluation, and lowered-IR emission. It builds the static library `libvexelfrontend.a` and the `vexel-frontend` CLI.

- CLI: `bin/vexel-frontend` (options: `-L/--emit-lowered`, `--allow-process` to opt into host commands, `-v`, `-o`)
- Lowered IR format: see `docs/frontend-lowered.md`
- Tests: `frontend/tests` (`run.sh` + shared harness; run via `make test`)
- Libraries: links against `libvexelfrontend.a`

## Pipeline & invariants

1. Parse + AST build.
2. Resolver: predeclare functions/types and validate annotations (binding stage).
3. TypeChecker: type inference and semantic validation.
4. Optimizer: constant folding + simplifications on the typed AST.
5. Analyzer: reachability, used globals/types, and reentrancy analysis.
6. Type-use validation: any *used* value must have a concrete type; unresolved types are allowed only when the value is unused. Compile-time-dead branches are ignored in this pass.

The validator depends on `Analyzer` facts; if pass ordering changes, update this contract.
