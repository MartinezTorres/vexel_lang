# Vexel Examples

The playground loads this folder recursively and exposes every file through the source navigator.

## Build/Run Pattern
From repo root:

```bash
make
./build/vexel -b c examples/simple.vx -o build/simple
gcc build/simple.c -o build/simple -lm
./build/simple
```

Use `-b megalinker` or `-b vexel` to inspect other backend outputs.

## C Backend Status

All `.vx` files in `examples/` currently compile with `-b c` except:

- `raytracer.vx` (known backend typing gap in C codegen; it compiles with `-b vexel`)

## Tutorial Examples

- `tutorial/` contains the curated learning examples used by the playground.
- Most files are single-file examples; `tutorial/multifile/` shows a multi-file module import.

## Library and Showcase

- `lib/` contains reusable helper modules (`math`, `print`, `vector`, `stack`, `set`, `config`, `counter`).
- `raytracer.vx` is a large showcase example and is validated with the `vexel` backend.

## Notes

- `-o` takes an output base path; backends emit one or more files from that base.
