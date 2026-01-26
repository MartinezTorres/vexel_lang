# Introduction

Vexel is a minimal, strongly typed language with **no keywords**. Everything is expressed using sigils and operators. The compiler is whole-program and aggressively evaluates code at compile time when possible. Only exported functions (`&^`) are visible outside the program.

What that means in practice:

- You declare types with `#` and functions with `&`.
- Conditionals use `?` and loops use `@`.
- Most code can run at compile time; unused functions and constants are removed.
- There is no standard library; you declare external functions with `&!` to call into your host environment.

A tiny taste of the syntax:

```vexel
&add(a:#i32, b:#i32) -> #i32 { a + b }

res:#i32 = add(40, 2)

&^main() -> #i32 { res }
```

Next: build the tools and run your first program.

- Prev: (start here)
- Next: [Build and Run](01-build-and-run.md)
