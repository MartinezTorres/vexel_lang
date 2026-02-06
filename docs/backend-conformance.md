# Backend Conformance Checks

Vexel ships a lightweight backend conformance kit to keep plugin-style
backends consistent with the expected build/CLI contract.

## What It Checks

For each discovered backend directory under `backends/*` and `backends/ext/*`
(excluding `common`, `_template`, and `ext` root):

- `Makefile` exists and provides `all`, `test`, and `clean` targets.
- `src/<backend>_backend.cpp` exists.
- `register_backend_<backend>()` is defined.
- `backend.info.name` matches directory/backend name.
- Backend dedicated CLI is built as `build/vexel-<backend>`.
- CLI responds to `-h`.
- CLI can compile a minimal Vexel source.

## Run

- As part of repository-wide tests: `make test`
- Standalone: `bash backends/tests/run_conformance.sh`

This suite is intentionally minimal and backend-agnostic; backend-specific
behavior remains covered by each backend's own tests.

