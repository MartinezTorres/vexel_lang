# Backend Authoring Guide

This guide explains how to implement a backend for Vexel using the current registry API. It covers in-tree and out-of-tree setups and the minimal CLI wiring.

## Quick checklist
- Implement `register_backend_<name>()`.
- Provide an emit function that writes output files.
- Register the backend in the CLI (and any backend-specific CLI).
- Build and test.

## Core interface
Backends register themselves with the frontend registry:

```cpp
// frontend/src/backend_registry.h
namespace vexel {
struct BackendInfo {
    std::string name;
    std::string description;
    std::string version;
};

struct BackendContext {
    const Module& module;
    TypeChecker& checker;
    const Compiler::Options& options;
    const Compiler::OutputPaths& outputs;
    const AnalysisFacts& analysis;
    const OptimizationFacts& optimization;
};

using BackendEmitFn = void (*)(const BackendContext& ctx);

struct Backend {
    BackendInfo info;
    BackendEmitFn emit;
};

bool register_backend(Backend backend);
const Backend* find_backend(const std::string& name);
std::vector<BackendInfo> list_backends();
}
```

## Minimal backend skeleton
Create a backend module that registers itself and emits files:

```cpp
// backends/<name>/src/<name>_backend.h
#pragma once
namespace vexel {
void register_backend_<name>();
}
```

```cpp
// backends/<name>/src/<name>_backend.cpp
#include "<name>_backend.h"
#include "backend_registry.h"
#include <fstream>

namespace vexel {

static void emit_<name>(const BackendContext& ctx) {
    std::string path = (ctx.outputs.dir / (ctx.outputs.stem + ".txt")).string();
    std::ofstream out(path);
    if (!out) {
        throw CompileError("Cannot write output: " + path, SourceLocation());
    }
    out << "backend=" << ctx.options.backend << "\n";
}

void register_backend_<name>() {
    Backend backend;
    backend.info.name = "<name>";
    backend.info.description = "My backend";
    backend.info.version = "v0.2.1";
    backend.emit = emit_<name>;
    (void)register_backend(backend);
}

}
```

Notes:
- Emit functions can throw `CompileError` for diagnostics; these are reported by the CLI.
- Use `ctx.outputs.dir` and `ctx.outputs.stem` to build filenames.
- Respect `ctx.options.verbose` for logging.
- `ctx.analysis` and `ctx.optimization` provide precomputed whole-program facts (reachability, mutability, reentrancy, constexpr values).

## Backend-specific options
The CLI supports a generic option pass-through:
```
--backend-opt key=value
```
Values are stored in `ctx.options.backend_options`. This allows third-party backends to add flags without touching the core CLI. Recommended patterns:

```cpp
// In your backend emit():
auto it = ctx.options.backend_options.find("caller_limit");
if (it != ctx.options.backend_options.end()) {
    int limit = std::stoi(it->second);
    // validate and apply
}
```

```cpp
// In your backend CLI (vexel-<name>):
if (std::strcmp(argv[i], "--backend-opt") == 0) { /* parse key=value and store */ }
```

## Template backend folder
There is a copy-and-rename starter at `backends/_template`:
- Copy it to `backends/<name>`.
- Rename `template_*` files to `<name>_*`.
- Replace `template` in the code with your backend name.
- Register it in `driver/src/vexel_main.cpp`.

## In-tree backend integration
1) Add a backend folder:
```
backends/<name>/
  Makefile
  src/
    <name>_backend.h
    <name>_backend.cpp
    <name>_main.cpp
```

2) Add a CLI wrapper (optional but recommended):
```cpp
// backends/<name>/src/<name>_main.cpp
#include "<name>_backend.h"
#include "compiler.h"
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    vexel::register_backend_<name>();
    vexel::Compiler::Options opts;
    opts.output_file = "out";
    opts.backend = "<name>";
    // Parse args like backends/c/src/c_main.cpp and call Compiler.
    // Keep this CLI backend-fixed.
    return 0;
}
```

3) Update the driver to register the backend:
- Add `#include "<name>_backend.h"` in `driver/src/vexel_main.cpp`.
- Call `register_backend_<name>()` alongside `register_backend_c()`.

4) Add a backend Makefile and link it in the build:
- Follow `backends/c/Makefile` as a reference.
- The driver `Makefile` already adds `-I../backends/*/src` include paths.

## Out-of-tree backend integration (static link)
You can keep the backend in a separate repo and link it into a local build of `vexel`:
1) Build the backend as a static library that depends on Vexel frontend headers.
2) Link the backend library into the `vexel` binary (or build a custom CLI).
3) Call your `register_backend_<name>()` function before invoking `Compiler`.

This is enough to validate the interface without dynamic loading.

## Output contract
Backends should:
- Consume the lowered module (see `docs/frontend-lowered.md`).
- Emit deterministic output files based on `outputs.stem`.
- Avoid writing outside the output directory.

## Errors and diagnostics
- Use `CompileError` with a `SourceLocation` when possible.
- Let other exceptions bubble up for fatal errors.

## Versioning
- Backend `info.version` should track the language version (currently `v0.2.1`).
- If you need backend-only fixes, bump the patch and document it in your backend README.

## Testing
- Use the existing test harness pattern from `docs/testing.md`.
- For backend tests, provide `tests/<suite>/<case>/run.sh` or `test.sh`.
- Keep tests deterministic and avoid requiring external tools unless documented.
