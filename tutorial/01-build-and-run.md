# Build and Run

From the repo root, build all tools:

```bash
make
```

The fastest path is the portable C backend (`vexel-c`). Compile a Vexel file to C, then build the C output:

```bash
./bin/vexel-c tutorial/hello.vx -o build/hello
gcc build/hello.c -o build/hello -lm
./build/hello
```

Notes:

- `-o` is the **output base name**; the compiler writes both `build/hello.c` and `build/hello.h`.
- The unified driver `./bin/vexel` also works: `./bin/vexel -b c tutorial/hello.vx`.
- Process expressions (compile-time shell commands) are disabled by default; enable with `--allow-process` only for trusted code.

- Prev: [Introduction](00-introduction.md)
- Next: [Hello, Vexel](02-hello-world.md)
