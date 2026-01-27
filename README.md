# Vexel Compiler

C++ implementation of the Vexel language front-end plus the portable C backend. The SDCC banked backend is currently out-of-tree.

**Status**: Language RFC v0.2.1 is implemented for the c backend.

## Quick Start

```bash
make                                  # build libs + all CLIs
./bin/vexel-c examples/simple.vx   # emit out.c/out.h with the c backend
gcc out.c -o simple -lm
./simple
```

## Components & Layout

- `frontend/` - lexer/parser/typechecker/evaluator/AST/codegen (`libvexelfrontend.a`, `bin/vexel-frontend`, tests).
- `backends/c/` - portable C backend (`libvexel-c.a`, `bin/vexel-c`, tests).
- `driver/` - unified `bin/vexel` CLI that lists registered backends.
- `docs/` - language docs (`docs/vexel-rfc.md`, `docs/frontend.md`, `docs/frontend-lowered.md`, `docs/backends.md`).
- `tutorial/` - step-by-step Vexel tutorial (Markdown, browsable on GitHub).
- `playground/` - WebAssembly playground build (compile-to-C visualization).
- `examples/` - sample programs plus `examples/lib/` helper modules; smoke tests in `examples/tests/`.

## Usage

```bash
./bin/vexel input.vx                    # unified driver (defaults to c when available)
./bin/vexel -b c input.vx               # pick a backend explicitly
./bin/vexel-c input.vx                  # backend-specific CLI (portable C)
./bin/vexel-frontend -L input.vx        # frontend only: emit lowered IR
./bin/vexel-frontend --allow-process foo.vx # opt in to process expressions (executes host commands)
```

Vexel emits C; compile the generated `.c` with your host toolchain (e.g., `gcc -std=c11 -O2 -lm`).

### Process Expressions (Safety)

Process expressions execute host commands. They are **disabled by default**; pass `--allow-process` when you trust the input. Keep them off for untrusted sources and CI.

## Requirements & Testing

- Suites live under `frontend/tests`, `backends/*/tests`, `core/tests`, `examples/tests`.
- Frontend tests use the Makefile harness; backend C tests use metadata inside `test.vx` files.
- Command: `make test` (builds and runs the full suite)

See `docs/testing.md` for the `run.sh` format and harness details.

## Web Playground

The web playground runs `vexel-c` in WebAssembly and emits C for visualization. The built `docs/index.html` is self-contained (compiler embedded).

Live playground: https://martineztorres.github.io/vexel_lang/

```bash
source /path/to/emsdk_env.sh
make web
python3 -m http.server
```

Open `http://localhost:8000/docs/` in a browser. You can copy `docs/index.html` to another machine and it will still work.

## Supported Platforms & Toolchains

- Builds require a C++17 compiler. The Makefiles honor `CXX`, so you can run `CXX=clang++ make` to select a toolchain.
- Generated C is compiled with a host C11 toolchain during runtime tests; defaults assume `gcc -std=c11 -O2 -lm`.
- Backends: c (portable C) is stable.

## Licensing & Releases

- License: MIT (see `LICENSE`).
- Status: RFC v0.2.1 implemented for c. Publish release notes in `CHANGELOG.md` as features land.

## Examples

Example programs live in `examples/`. Optional helper modules are in `examples/lib/` (import with `::lib::print;`, `::lib::vector;`, etc.). Example smoke tests run with the main test harness (`make test`).
