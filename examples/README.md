# Vexel Examples

Examples are grouped by theme so you can quickly find a sample to start from. All paths are relative to `examples/`.

## Build/Run Pattern
From the repo root:
```bash
make                                       # build all CLIs
./bin/vexel-c examples/simple.vx -o build/simple
gcc build/simple.c -o build/simple -lm     # generates build/simple.{c,h}
./build/simple
```
Use the same pattern for other examples by swapping the input filename and output base name.

### Standard Library Samples
Additional helper modules live under `examples/lib/` (e.g., `print.vx`, `vector.vx`). Import them from examples with:
```vexel
::lib::print;
::lib::vector;
```

## Core Language
- `simple.vx` – compile-time arithmetic folded out of the binary.
- `demo.vx` – compile-time math plus small array operations.
- `comprehensive.vx` – larger compile-time example mixing arrays and globals.
- `compile_time_demo.vx` – struct construction and arithmetic fully evaluated at compile time.
- `arrays.vx`, `arrays_simple.vx` – array literals, indexing, and sums.
- `range.vx` – range operator example.

## Strings & Printing
- `strings.vx`, `chars.vx`, `char_lit.vx` – basic string/char literals.
- `print_strings.vx` – print an array of strings with external `putchar`.
- `print_strings_simple.vx` – minimal string printing without range helpers.

## Data Structures & Methods
- `vector_demo.vx` – manual vector-of-bytes printing.
- `vector_strings.vx` – uses `lib::vector` and `lib::print` to print strings.
- `custom_iteration.vx` – custom `@` and `@@` iterator methods on a user type.
- `operator_methods.vx` – operator overloading (`Vec2::+`, `Vec2::==`) on a named type.

## Additional Samples
- `raytracer.vx` – full raytracer exercising floats, structs, and external I/O.
- `compile_time_demo.vx` – compile-time heavy workload (also in core list for emphasis).

## Lexer Edge Cases
- `lexer_edge_cases/` – 11 tiny files covering EOF handling, escapes, unterminated literals, and whitespace-only input.

Notes:
- `-o` takes an output **base name**; the compiler writes both `.c` and `.h`.
- Some samples rely on external `putchar`; add `-DSTANDALONE` when compiling if your environment expects that flag.
