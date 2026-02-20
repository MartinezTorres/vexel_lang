# Declaration Requirements Test Suite

Tests for declaration rules (DC-xxx lineage) that run through the frontend pipeline.

Per-test files:

- `test.vx` (input module)
- optional `test.sh` (custom assertions instead of plain compile)
- `expected_report.md` (golden output)

Run with `make frontend-test` (repo root) or `make -C frontend test`.
