# Vexel Backends

Backends consume the lowered frontend IR and emit target-specific outputs. Each backend ships as a static library plus a thin CLI; the unified `vexel` driver lists registered backends.

- **c** (`libvexel-c.a`, `bin/vexel-c`): portable C targeting 32-bit x86 System V. See `backends/c/README.md`. Tests in `backends/c/tests`.

The SDCC banked backend is maintained out-of-tree for now.

Plugin interface overview: `doc/backend-plugin.md`.
Backend authoring guide: `doc/backend-authoring.md`.
Template backend starter: `backends/_template`.

Run backend suites with `make test`.
