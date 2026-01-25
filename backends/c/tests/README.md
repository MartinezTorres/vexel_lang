C backend tests live here (codegen structure plus runtime/semantic checks using `{VEXEL_C}`).

- Suites: `general/`, `runtime/`, `backend_c/`
- Metadata: each `test.vx` includes `@command`, `@expect-exit`, and optional `@run-generated` / `@expect-stderr`
- Run with `make test` (invokes `tests/run_tests.py`)
