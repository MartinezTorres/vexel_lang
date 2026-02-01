# Hello, Vexel

Vexel has no standard library, so I/O is done via **external functions**. The portable C backend expects C ABI functions, so you can call `putchar` directly.

```vexel
&!putchar(c:#u8);

&print_newline() {
  putchar(10);
}

&^main() -> #i32 {
  putchar(72);  // H
  putchar(101); // e
  putchar(108); // l
  putchar(108); // l
  putchar(111); // o
  print_newline();
  0
}
```

Key points:

- `&!name(...)` declares an **external** function.
- `&^main()` marks an **exported** function; this is the entry point.
- A function returns its last expression unless you use `-> expr;` explicitly.

- Prev: [Build and Run](01-build-and-run.md)
- Next: [Values and Types](03-values-and-types.md)
