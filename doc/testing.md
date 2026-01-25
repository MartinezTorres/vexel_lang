# Testing

The test suite is entirely self-describing: every test directory contains a `run.sh` that declares what it checks and where the behaviour is defined in the RFC. There is no separate requirements catalog or status file to keep in sync.

## Layout

- `frontend/tests/` – lexer/parser/typechecker/evaluator
- `backends/c/tests/` – portable C backend (codegen + runtime)
- `backends/banked/tests/` – SDCC banked backend
- `backends/common/tests/` – shared backend plumbing
- `core/tests/` – core utilities
- `examples/tests/` – smoke tests for sample programs

## Test Format (`run.sh`)

Each test directory has a `run.sh` with the command, expected behaviour, and RFC reference. The script is self-contained: it expands tool macros, runs the compiler, optionally compiles/executes generated C, and checks exit/stdout/stderr.

```bash
#!/usr/bin/env bash
# RFC: doc/vexel-rfc.md#expressions--control
# DESC: Process expressions are rejected by default (unless --allow-process is passed)
RFC_REF="doc/vexel-rfc.md#expressions--control"
DESC="Process expressions are rejected by default (unless --allow-process is passed)"
CMD=("{VEXEL_FRONTEND}" "input.vx")
EXPECT_EXIT=1
EXPECT_STDOUT=""
EXPECT_STDERR="Process expressions are disabled"
RUN_GENERATED=0
# The script defines its own harness below; just execute it.
```

Fields:
- `RFC_REF`: RFC section that defines the behaviour.
- `DESC`: short description of what the test checks.
- `CMD`: command to run; tool macros (`{VEXEL}`, `{VEXEL_FRONTEND}`, `{VEXEL_C}`, `{VEXEL_BANKED}`) expand to the built binaries.
- `EXPECT_EXIT`: expected exit code (default 0).
- `EXPECT_STDOUT` / `EXPECT_STDERR`: substring checks (blank means no check).
- `RUN_GENERATED`: set to `1` to compile and run the emitted C with `gcc -std=c11 -O2 -lm` after a successful compile.

## Running Tests

- Full suite: `make test` (Make finds every `run.sh` under the test directories and executes them in sorted order).

All suites run together; there is no draft/active filtering. A non-zero exit means at least one test failed; see the emitted diagnostics for details.
