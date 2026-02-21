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

Examples evolve with frontend/backend refactors. Do not assume every `.vx` file in this tree compiles with every backend at all times.

To check current status locally:

```bash
find examples -name '*.vx' -print0 | xargs -0 -n1 ./build/vexel -b c
```

## Tutorial Examples

- `tutorial/` contains the curated learning examples used by the playground.
- `tutorial/manifest.json` defines tutorial order/title/summary for playground navigation.
- Most files are single-file examples; `tutorial/multifile/` shows a multi-file module import.

## Library and Showcase

- `lib/` contains reusable helper modules (`math`, `print`, `vector`, `stack`, `set`, `config`, `counter`).
- `raytracer.vx` is a large showcase example.

## Notes

- `-o` takes an output base path; backends emit one or more files from that base.
