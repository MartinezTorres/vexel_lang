# Frontend Test Suite

Tests for lexing, parsing, type-checking, analysis, and compile-time execution live here.

Harness contract:

- Each test directory contains either `test.vx` (driver-based compile) or `test.sh` (custom assertions).
- Every test directory contains `expected_report.md`.
- Reports are generated under `build/frontend-tests/...` by `frontend/Makefile` and compared against `expected_report.md`.

Run:

- `make frontend-test` (from repo root), or
- `make -C frontend test`

Backend/runtime behaviour is covered under `backends/*/tests`.
