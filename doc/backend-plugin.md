# Backend Plugin Method (Draft)

This document captures the current plugin method for Vexel backends. The goal is a small, stable API that lets backends live in-tree or out-of-tree while keeping the compiler core simple. For step-by-step instructions, see `doc/backend-authoring.md`.

## Goals
- Backends register themselves by name and expose a single emit entry point.
- The unified `vexel` CLI lists and selects backends at runtime.
- Out-of-tree backends can reuse the same interface without forking core.

## Non-goals (for now)
- Dynamic loading of shared libraries.
- A formal compatibility checker between backend and compiler versions.

## Core API (C++)
Backends register with the frontend registry and provide an emit function.

```cpp
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
    const std::unordered_set<std::string>& non_reentrant_funcs;
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

## Backend responsibilities
- Build any backend-specific IR or codegen from the `Module` and `TypeChecker`.
- Emit files under `outputs.dir` using `outputs.stem` as the base filename.
- Respect `options.verbose` for logging.

## Discovery model
Current model is static registration:
- Each backend provides a `register_backend_<name>()` function.
- The CLI calls these registration functions at startup.
- The registry is used to list backends and resolve `-b <name>`.

This keeps the build simple and makes out-of-tree backends possible without adding runtime loading.

## Out-of-tree backend pattern
Recommended layout for external backends:
- A small library that links against the Vexel frontend and implements `register_backend_<name>()`.
- An optional CLI wrapper (similar to `vexel-c`) that registers the backend and invokes `Compiler`.
- If integrated into this repo, add the backend under `backends/<name>/` and hook it into the build.

## Future extension (optional)
Dynamic discovery can be layered on later:
- Search `VEXEL_BACKENDS_PATH` for shared libraries.
- `dlopen` each and call a well-known symbol (e.g. `register_backend_plugin`).
- Keep the static registry as the final in-process interface.

## Migration plan for the banked backend
1. Keep banked out-of-tree while stabilizing the API.
2. Have the banked repo provide `register_backend_banked()`.
3. Add a small integration recipe for linking or loading it.
