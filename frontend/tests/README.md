# Frontend Test Suite

Tests for lexing, parsing, type-checking, and evaluation live here. Each test directory contains a `run.sh` with the RFC reference and expected behaviour; `{VEXEL_FRONTEND}` expands to `bin/vexel-frontend`.

- Run the suite via `make test`
- Backend/runtime behaviour is covered under `backends/*/tests`
- Manual lexer fixtures are in `frontend/tests/lexer_edge_cases/` with `test_runner.sh`.
