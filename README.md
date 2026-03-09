# Vexel Compiler

**Role**: Repository threshold and reading map.

Vexel is a whole-program, compile-time-first language that gives up raw-pointer escape hatches so the compiler can preserve global reasoning, then lowers through backends (including portable C).

Vexel exists to keep program logic, compile-time execution, optimization intent, and emitted artifacts in one codebase instead of splitting semantics across generator layers and side tools.

## Minimal Example

```vx
&add(a:#i32, b:#i32) -> #i32 { a + b }
res:#i32 = add(40, 2)
&^main() -> #i32 { res }
```

This folds to a constant path before backend emission.

## Fastest Way In

- **Playground**: https://martineztorres.github.io/vexel_lang/ (open `Playground` route)
- **Local quick start**:

```bash
make
./build/vexel -b c examples/simple.vx
gcc out.c -o simple -lm
./simple
```

- **Tutorial route**: `docs/tutorial.md` or `docs/playground.html` with `examples/tutorial/*`.

## Reading Map

Use this order:

1. **Read the law**: [RFC](docs/vexel-rfc.md) (normative source of truth).
2. **Read elaboration**: [Detailed spec](docs/spec/index.md) (operational semantics and contracts).
3. **Taste behavior**: [Tutorial](docs/tutorial.md) + [Playground](docs/playground.html).
4. **Open the machine**: [Architecture](docs/architecture.md).
5. **Understand refusals**: [Anti-goals](docs/anti-goals.md).
6. **Modify implementation**: sections below (layout, pipeline, backend contract, tests).

## Repository Structure

- `frontend/` — lexer/parser/typechecker/evaluator/AST + analysis pipeline (`libvexelfrontend.a`, `build/vexel-frontend`, tests).
- `backends/c/` — portable C backend (`libvexel-c.a`, tests).
- `backends/ext/` — optional local external backends discovered by build/driver (for example `megalinker` when present).
- `driver/` — unified `build/vexel` CLI and backend dispatch.
- `docs/` — landing (`docs/index.html`), RFC, detailed spec, tutorial, architecture, anti-goals, generated playground page.
- `playground/` — WebAssembly playground build (self-contained `docs/playground.html`).
- `std/` — bundled standard-library module fallback (`::std::*` imports; project-local `std/` overrides per module path).
- `examples/` — sample programs plus tutorial corpus.

## Documentation Policy

- `docs/vexel-rfc.md` is normative.
- `docs/spec/index.md` elaborates RFC behavior.
- `README.md` is the repository map and operational guide.
- Compiler/backend behavior is documented next to owning code paths.

Implementation index:

- Frontend pipeline and pass order: `frontend/src/cli/compiler.cpp`
- Frontend architecture contract and change strategy: `frontend/src/architecture.md`
- Frontend invariant checks: `frontend/src/pipeline/pass_invariants.h`, `frontend/src/pipeline/pass_invariants.cpp`
- Lowered frontend contract: `frontend/src/transform/lowerer.h`
- C backend code generator core: `backends/c/src/codegen.h`
- Backend plugin API: `frontend/src/support/backend_registry.h`
- Driver CLI contract: `driver/src/vexel_main.cpp`
- Backend discovery/build wiring: `Makefile`, `driver/Makefile`, `playground/Makefile`
- Test harnesses: `frontend/Makefile`, `backends/c/tests/run_tests.py`, `backends/conformance_test.sh`

## Frontend Pipeline

Canonical stage order in `frontend/src/cli/compiler.cpp`:

1. Module load (`ModuleLoader`)
2. Resolve symbols/scopes (`Resolver`)
3. Type check (`TypeChecker`)
4. Monomorphize generics (`Monomorphizer`)
5. Lower typed AST (`Lowerer`)
6. Collect optimization facts (`Optimizer`)
7. Analyze reachability/effects/reentrancy (`Analyzer`)
8. Validate concrete type usage (`TypeChecker::validate_type_usage`)
9. Prune merged top-level declarations to the live set (reachable functions, used globals/types)
10. Emit backend output

Lowered frontend contract lives in `frontend/src/transform/lowerer.h`.
Debug invariant checks live in `frontend/src/pipeline/pass_invariants.h`.

## Usage

```bash
./build/vexel -b c input.vx                     # unified driver (backend selection is required)
./build/vexel -b c --emit-analysis input.vx     # emit analysis report
./build/vexel -b c --type-strictness=1 input.vx # require explicit type annotations for new variables
./build/vexel -b c --strict-types=full input.vx # full strict mode (equivalent to --type-strictness=2)
./build/vexel -b megalinker --backend-opt caller_limit=8 input.vx # when local megalinker backend is present
./build/vexel -b c --run input.vx               # optional: run via libtcc
./build/vexel -b c --emit-exe -o app input.vx   # optional: emit native exe via libtcc
./build/vexel-frontend input.vx                 # frontend-only validation (no backend emission)
./build/vexel-frontend --allow-process foo.vx   # opt in to process expressions
```

Vexel emits C; compile the generated `.c` with your host toolchain (for example `gcc -std=c11 -O2 -lm`).

The unified driver forwards unknown options to the selected backend. If neither frontend nor selected backend recognizes an option, compilation fails with combined usage output.
Backend selection is always explicit: pass `-b <name>` for every compile mode (`--run` / `--emit-exe` included).
`--type-strictness` levels: `0` relaxed unresolved integer flow, `1` requires explicit type annotations for new variables, `2` additionally rejects unresolved literal flow across inferred call boundaries.

## Backend Extension Contract

Backend plugin API is defined in `frontend/src/support/backend_registry.h`.

Backend architecture rule:

- The frontend/backend boundary is the `AnalyzedProgram` contract.
- Backend code generation is backend-owned (no shared codegen layer).
- Backends may diverge in lowering strategy and target behavior.
- Reentrancy is analyzed in the frontend graph pass; each backend documents boundary defaults and whether it consumes `[[nonreentrant]]`.

Current reentrancy contract:

- C backend (`backends/c/README.md`): recognizes `[[nonreentrant]]`; defaults to reentrant entry/exit boundaries (`R/R`).
- External backends under `backends/ext/<name>/` define their own reentrancy defaults in local README.

Each backend must provide:

- `register_backend_<name>()`
- `Backend.info` (`name`, `description`, `version`)
- `Backend.emit`

Optional unified-driver hooks:

- `Backend.parse_option` for backend-specific CLI flags in `build/vexel`
- `Backend.print_usage` for backend-specific help lines

Placement/discovery:

- In-tree: `backends/<name>/`
- Local external: `backends/ext/<name>/`
- Required Makefile targets: `all`, `test`, `clean`

Conformance script: `backends/conformance_test.sh`.

### Process Expressions (Safety)

Process expressions execute host commands. They are **disabled by default**; pass `--allow-process` only for trusted inputs.

## Requirements & Testing

- Suites live under `frontend/tests` and `backends/*/tests` (plus backend conformance in `backends/conformance_test.sh`).
- Frontend tests use the Makefile harness; backend C tests use metadata inside `test.vx` files.
- `make test` builds and runs full suite.
- `make ci` runs release gate aggregate (`test` + frontend perf guards).
- Focused suites: `make frontend-test`, `make frontend-perf-test`, `make backend-c-test`, `make backend-conformance-test`.

## Web Playground

The web playground runs the unified compiler in WebAssembly, lets you choose a backend, and shows emitted output files. Generated `docs/playground.html` is self-contained.

Live playground: https://martineztorres.github.io/vexel_lang/

```bash
source /path/to/emsdk_env.sh
make web
python3 -m http.server
```

Open `http://localhost:8000/docs/` for landing+routes, and `http://localhost:8000/docs/playground.html` for direct playground.

GitHub Pages deployment runs `make web` and publishes `docs/`.
Landing (`docs/index.html`) is source-controlled; playground (`docs/playground.html`) is generated from `playground/playground.template.html`.

## Supported Platforms & Toolchains

- Requires a C++17 compiler. Makefiles honor `CXX` (`CXX=clang++ make` supported).
- Generated C is compiled with host C11 toolchain in runtime tests (default assumes `gcc -std=c11 -O2 -lm`).
- Backends: `c` is stable.
- Optional native mode: if `libtcc` and runtime files (`libtcc1.a`) are detected, `build/vexel` supports `--run` and `--emit-exe` with backend `c`.

## Licensing & Releases

- License: MIT (`LICENSE`).
- Status: RFC v1.0-rc1 implemented for `c`; release notes land in `CHANGELOG.md`.
