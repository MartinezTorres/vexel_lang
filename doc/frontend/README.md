# Vexel Frontend

The frontend performs lexing, parsing, type checking, constant evaluation, and lowered-IR emission. It builds the static library `libvexelfrontend.a` and the `vexel-frontend` CLI.

- CLI: `bin/vexel-frontend` (options: `-L/--emit-lowered`, `--allow-process` to opt into host commands, `-v`, `-o`)
- Lowered IR format: see `doc/frontend/lowered.md`
- Tests: `frontend/tests` (`run.sh` + shared harness; run via `make test`)
- Libraries: links against `libvexelfrontend.a`
