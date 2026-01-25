# Vexel Compiler

C++ implementation of the Vexel language front-end plus multiple backends (portable C/x86 and an experimental SDCC banked target).

**Status**: Language RFC v0.2.1 is implemented for the c backend. The banked backend is experimental (see `backends/banked/README.md`).

## Quick Start

```bash
make                                  # build libs + all CLIs
./bin/vexel-c examples/simple.vx   # emit out.c/out.h with the c backend
gcc out.c -o simple -lm
./simple
```

## Components & Layout

- `frontend/` – lexer/parser/typechecker/evaluator/AST/codegen (`libvexelfrontend.a`, `bin/vexel-frontend`, tests).
- `backends/c/` – portable C backend (`libvexel-c.a`, `bin/vexel-c`, tests).
- `backends/banked/` – SDCC banked backend (`libvexel-banked.a`, `bin/vexel-banked`, tests, `include/megalinker.h`).
- `driver/` – unified `bin/vexel` CLI that auto-discovers backends under `backends/*`.
- `doc/` – language docs (`doc/vexel-rfc.md`, `doc/frontend/*`). Backend docs in respective backend directories.
- `examples/` – sample programs plus `examples/lib/` helper modules; smoke tests in `examples/tests/`.

## Usage

```bash
./bin/vexel input.vx                    # unified driver (defaults to c when available)
./bin/vexel -b banked input.vx          # pick a backend explicitly
./bin/vexel-c input.vx               # backend-specific CLI (portable C)
./bin/vexel-banked input.vx             # backend-specific CLI (SDCC banked)
./bin/vexel-frontend -L input.vx        # frontend only: emit lowered IR
./bin/vexel-frontend --allow-process foo.vx # opt in to process expressions (executes host commands)
```

Vexel emits C; compile the generated `.c` with your host toolchain (e.g., `gcc -std=c11 -O2 -lm`).

### Process Expressions (Safety)

Process expressions execute host commands. They are **disabled by default**; pass `--allow-process` when you trust the input. Keep them off for untrusted sources and CI.

## Requirements & Testing

- Suites live under `frontend/tests`, `backends/*/tests`, `core/tests`, `examples/tests`.
- Each test directory has a `run.sh` describing the RFC reference and expected behaviour; the shared harness runs the command and (optionally) compiles/executes generated C.
- Command: `make test` (builds and runs the full suite; Make finds all `run.sh` tests)

See `doc/testing.md` for the `run.sh` format and harness details.

## Supported Platforms & Toolchains

- Builds require a C++17 compiler. The Makefiles honor `CXX`, so you can run `CXX=clang++ make` to select a toolchain.
- Generated C is compiled with a host C11 toolchain during runtime tests; defaults assume `gcc -std=c11 -O2 -lm`.
- Backends: c (portable C) is stable; banked (SDCC + Megalinker) is experimental.

## Licensing & Releases

- License: MIT (see `LICENSE`).
- Status: RFC v0.2.1 implemented for c; banked backend remains experimental. Publish release notes in `CHANGELOG.md` as features land.

## Examples

Example programs live in `examples/`. Optional helper modules are in `examples/lib/` (import with `::lib::print;`, `::lib::vector;`, etc.). Example smoke tests run with the main test harness (`make test`).
