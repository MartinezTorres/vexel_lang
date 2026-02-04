# Vexel Backends

Backends consume the lowered frontend IR and emit target-specific outputs. Each backend ships as a static library plus a thin CLI; the unified `vexel` driver lists registered backends.

- **c** (`libvexel-c.a`, `bin/vexel-c`): portable C targeting 32-bit x86 System V. See `backends/c/README.md`. Tests in `backends/c/tests`.

The SDCC banked backend is maintained out-of-tree for now.

Plugin interface overview: `docs/backend-plugin.md`.
Backend authoring guide: `docs/backend-authoring.md`.
Template backend starter: `backends/_template`.

Run backend suites with `make test`.

## Backend-specific options
The unified `vexel` CLI accepts backend-specific options without changing core flags:
```
--backend-opt key=value
```
These are stored in `ctx.options.backend_options` (a string map) and are intended for out-of-tree backends. A backend-specific CLI should parse the same `--backend-opt` flag and populate the map before invoking `Compiler`.

## Preferred workflow for new backends
Vexel favors out-of-tree backend development to keep core changes separate:

- Create a standalone backend repo.
- Add Vexel as a dependency (recommended: submodule at `deps/vexel/`, or a vendored SDK snapshot).
- Build against the frontend headers and `libvexelfrontend.a` from that dependency.
- Provide your own backend-fixed CLI (`vexel-<name>`) inside the backend repo.
- For local integration testing with the unified `vexel` driver, add the backend repo as a submodule under `backends/<name>` in a Vexel checkout and register it in `driver/src/vexel_main.cpp`.

In-tree backends are still supported (the C backend is the reference), but the preferred path is to keep new backends in their own repo and link to Vexel as a dependency.

### Minimal SDK snapshot
If you don't want a submodule, a lightweight SDK snapshot works well:
- `include/` (copy of required `frontend/src` headers)
- `lib/libvexelfrontend.a`

Keep these under `deps/vexel-sdk/` in your backend repo, update them when you bump Vexel.

### Minimal Makefile sketch
```make
VEXEL_SDK ?= deps/vexel
INCLUDES += -I$(VEXEL_SDK) -I$(VEXEL_SDK)/frontend/src
LIBS += $(VEXEL_SDK)/build/lib/libvexelfrontend.a
```
