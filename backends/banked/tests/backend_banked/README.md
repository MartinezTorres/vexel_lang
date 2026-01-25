# Backend Banked Test Suite

Tests for the SDCC banked backend (BK-xxx). Each directory contains `run.sh` plus inputs; `{VEXEL_BANKED}` expands to `bin/vexel-banked`.

- Specification: `backends/banked/README.md`
- Run with `make test` (Make finds all `run.sh` tests)
- Output artifacts include generated C/headers and Megalinker integration; `backends/banked/include/megalinker.h` ships with the backend.
