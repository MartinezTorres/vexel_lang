# Functions and Compile-Time

Functions are declared with `&` and return the last expression by default.

```vexel
&add(a:#i32, b:#i32) -> #i32 {
  a + b
}

&multiply(a:#i32, b:#i32) -> #i32 {
  a * b
}
```

### Exported and external

- `&^name` is **exported** (visible outside the program).
- `&!name` is **external** (implemented by the host toolchain).

```vexel
&!putchar(c:#u8);

&^main() -> #i32 {
  putchar(65);
  0
}
```

### Compile-time evaluation

The compiler tries to evaluate anything it can at compile time. Global constants must be compile-time values, and any pure helper functions used to compute them will be evaluated and then eliminated from the output.

```vexel
&compute(x:#i32, y:#i32) -> #i32 {
  (x + y) * 2
}

res:#i32 = compute(10, 20); // evaluated at compile time

&^main() -> #i32 {
  res
}
```

### Globals

- **Immutable globals**: `name[:Type] = expr` (compile-time value)
- **Mutable globals**: `name:Type` (uninitialized until assigned)

```vexel
counter:#i32;      // mutable global
answer:#i32 = 42;  // immutable global
```

- Prev: [Values and Types](03-values-and-types.md)
- Next: [Control Flow and Iteration](05-control-flow.md)
