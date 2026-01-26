# Vexel Tutorial

This tutorial is a guided, hands-on introduction to the Vexel language as implemented in this repo. It targets the portable C backend (`vexel-c`) and stays within the current RFC.

If you already know the basics, jump to `examples/` or the language spec in `docs/vexel-rfc.md`.

## Chapters

0. [Introduction](00-introduction.md)
1. [Build and Run](01-build-and-run.md)
2. [Hello, Vexel](02-hello-world.md)
3. [Values and Types](03-values-and-types.md)
4. [Functions and Compile-Time](04-functions-and-compile-time.md)
5. [Control Flow and Iteration](05-control-flow.md)
6. [Structs and Methods](06-structs-and-methods.md)
7. [Modules and Resources](07-modules-and-resources.md)
8. [Next Steps](08-next-steps.md)

## Conventions in examples

- Types use the `#` sigil (`#i32`, `#s`, `#Vec2`, `#i32[4]`).
- Functions use `&` (`&name`), exported functions use `&^`, externals use `&!`.
- No keywords: conditionals and loops are operators (`?` and `@`).
- Blocks `{ ... }` evaluate to their last expression.
