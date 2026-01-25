# Vexel Backends

Backends consume the lowered frontend IR and emit target-specific outputs. Each backend ships as a static library plus a thin CLI; the unified `vexel` driver auto-discovers backends under `backends/*`.

- **c** (`libvexel-c.a`, `bin/vexel-c`): portable C targeting 32-bit x86 System V. See `backends/c/README.md`. Tests in `backends/c/tests`.
- **banked** (`libvexel-banked.a`, `bin/vexel-banked`): SDCC banked target with Megalinker integration. Ships `backends/banked/include/megalinker.h`. See `backends/banked/README.md`. Tests in `backends/banked/tests`.

Run backend suites with `make test`.
