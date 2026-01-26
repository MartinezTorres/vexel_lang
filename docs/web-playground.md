# Web Playground Notes

This document collects build and integration notes for the fully client-side playground.

## Build output

- `playground/playground.template.html` is the editable source.
- `make web` generates `docs/index.html`, which embeds the compiler (JS + wasm) into a single file.

## Emscripten entrypoint on Ubuntu

When using the Ubuntu-packaged Emscripten toolchain, the emitted entrypoint symbol is `__main_argc_argv`. The web build exports that symbol so the runtime can invoke `main` via `callMain`.

If you remove or change this, the playground will load but produce no outputs (the compiler never runs).
