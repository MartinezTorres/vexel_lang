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

## Files That Currently Compile Cleanly

- `arrays_simple.vx`
- `bmp_to_matrix.vx` (uses `assets/random_256x192.bmp`)
- `compile_time_demo.vx`
- `comprehensive.vx`
- `custom_iteration.vx`
- `demo.vx`
- `operator_methods.vx`
- `print_strings_simple.vx`
- `simple.vx`

## Playground-Curated Examples

- `playground/` contains the curated examples that were previously hardcoded in the web playground (including `sieve`, `structs`, `typeof_demo`, `abi_hints`, and others).
- Each subfolder is an entry example (`main.vx`), with local helper files/assets when needed (for example `playground/bmp_matrix/assets/random_256x192.bmp`).

## Workbench Files

Some files here are kept for parser/runtime exploration and may fail with the current grammar/semantics:

- `arrays.vx`, `range.vx`
- `char_lit.vx`, `chars.vx`, `strings.vx`
- `print_strings.vx`
- `raytracer.vx`
- `vector_demo.vx`, `vector_strings.vx`
- `lib/` modules (except `config.vx`, `counter.vx`)

## Notes

- `-o` takes an output base path; backends emit one or more files from that base.
- `lexer_edge_cases/` contains lexer-focused fixtures plus `test_runner.sh`.
