# Vexel Compiler

C++ implementation of the Vexel language front-end plus the portable C backend. The SDCC banked backend is currently out-of-tree.

**Status**: Language RFC v0.2.1 is implemented for the c backend.

## Quick Start

```bash
make                                  # build libs + all CLIs
./build/vexel-c examples/simple.vx    # emit out.c/out.h with the c backend
gcc out.c -o simple -lm
./simple
```

## Components & Layout

- `frontend/` - lexer/parser/typechecker/evaluator/AST/codegen (`libvexelfrontend.a`, `build/vexel-frontend`, tests).
- `backends/c/` - portable C backend (`libvexel-c.a`, `build/vexel-c`, tests).
- `driver/` - unified `build/vexel` CLI that lists registered backends.
- `docs/` - language RFC (`docs/vexel-rfc.md`) and generated playground page (`docs/index.html`).
- `tutorial/` - step-by-step Vexel tutorial (Markdown, browsable on GitHub).
- `playground/` - WebAssembly playground build (compile-to-C visualization).
- `examples/` - sample programs plus `examples/lib/` helper modules.

## Documentation Policy

- `docs/vexel-rfc.md` is the normative language specification.
- `README.md` is the operational guide (build, CLI, extension points, tests).
- Compiler/backend behavior is documented next to the owning code path.

Implementation index:

- Frontend pipeline and pass order: `frontend/src/compiler.cpp`
- Frontend invariant checks: `frontend/src/pass_invariants.h`, `frontend/src/pass_invariants.cpp`
- Lowered output contract: `frontend/src/lowerer.h`, `frontend/src/lowered_printer.h`
- Backend plugin API: `frontend/src/backend_registry.h`
- Driver CLI contract: `driver/src/vexel_main.cpp`
- Backend discovery/build wiring: `Makefile`, `driver/Makefile`, `playground/Makefile`
- Test harnesses: `frontend/Makefile`, `backends/c/tests/run_tests.py`, `backends/tests/run_conformance.sh`

## Frontend Pipeline

Canonical stage order in `frontend/src/compiler.cpp`:

1. Module load (`ModuleLoader`)
2. Resolve symbols/scopes (`Resolver`)
3. Type check (`TypeChecker`)
4. Monomorphize generics (`Monomorphizer`)
5. Lower typed AST (`Lowerer`)
6. Collect optimization facts (`Optimizer`)
7. Analyze reachability/effects/reentrancy (`Analyzer`)
8. Validate concrete type usage (`TypeChecker::validate_type_usage`)
9. Emit backend output

Lowered output contract lives in `frontend/src/lowerer.h` and `frontend/src/lowered_printer.h`.
Debug invariant checks live in `frontend/src/pass_invariants.h`.

## Usage

```bash
./build/vexel -b c input.vx                     # unified driver (backend selection is required)
./build/vexel -b c --emit-analysis input.vx     # emit analysis report
./build/vexel -b c -L input.vx                  # emit lowered Vexel + backend output
./build/vexel-c input.vx                        # backend-specific CLI (portable C)
./build/vexel-frontend input.vx                 # frontend-only lowered output
./build/vexel-frontend --allow-process foo.vx   # opt in to process expressions
```

Vexel emits C; compile the generated `.c` with your host toolchain (e.g., `gcc -std=c11 -O2 -lm`).

The unified driver forwards unknown options to the selected backend. If neither the frontend nor that backend recognizes an option, compilation fails with combined usage output.

## Backend Extension Contract

Backend plugin API is defined in `frontend/src/backend_registry.h`.

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
- Dedicated backend CLI: `build/vexel-<name>`

Conformance script: `backends/tests/run_conformance.sh`.

### Process Expressions (Safety)

Process expressions execute host commands. They are **disabled by default**; pass `--allow-process` when you trust the input. Keep them off for untrusted sources and CI.

## Requirements & Testing

- Suites live under `frontend/tests`, `backends/*/tests`, and `backends/tests` (conformance).
- Frontend tests use the Makefile harness; backend C tests use metadata inside `test.vx` files.
- `make test` builds and runs the full suite.
- `make frontend-test`, `make backend-c-test`, and `make backend-conformance-test` run focused suites.

## Web Playground

The web playground runs `vexel-c` in WebAssembly and emits C for visualization. The built `docs/index.html` is self-contained (compiler embedded).

Live playground: https://martineztorres.github.io/vexel_lang/

```bash
source /path/to/emsdk_env.sh
make web
python3 -m http.server
```

Open `http://localhost:8000/docs/` in a browser. You can copy `docs/index.html` to another machine and it will still work.
Playground build and embed logic live in `playground/Makefile`, `playground/embed.py`, and `playground/web_main.cpp`.

## Supported Platforms & Toolchains

- Builds require a C++17 compiler. The Makefiles honor `CXX`, so you can run `CXX=clang++ make` to select a toolchain.
- Generated C is compiled with a host C11 toolchain during runtime tests; defaults assume `gcc -std=c11 -O2 -lm`.
- Backends: c (portable C) is stable.

## Licensing & Releases

- License: MIT (see `LICENSE`).
- Status: RFC v0.2.1 implemented for c. Publish release notes in `CHANGELOG.md` as features land.

## Examples

Example programs live in `examples/`. Optional helper modules are in `examples/lib/` (import with `::lib::print;`, `::lib::vector;`, etc.). Example smoke tests run with the main test harness (`make test`).
