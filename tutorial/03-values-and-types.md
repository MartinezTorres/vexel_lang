# Values and Types

Types are always spelled with `#`. The most common primitives are:

- Integers: `#i8`, `#i16`, `#i32`, `#i64`, `#u8`, `#u16`, `#u32`, `#u64`
- Floats: `#f32`, `#f64`
- Boolean: `#b` (only `0` or `1`)
- String: `#s` (immutable, compile-time constant)

### Declarations

```vexel
x:#i32 = 10;
flag:#b = 1;
name:#s = "Vexel";
```

### Arrays

Arrays carry their length in the type: `#T[N]`.

```vexel
nums:#i32[3] = [1, 2, 3];
first:#i32 = nums[0];
len:#i32 = |nums|; // length is a compile-time constant
```

### Literal inference and casts

Integer literals are inferred as the smallest fitting type (e.g., `0`/`1` as `#b`, `2..255` as `#u8`). Use explicit casts when needed:

```vexel
c:#u8 = (#u8)65; // 'A'
```

- Prev: [Hello, Vexel](02-hello-world.md)
- Next: [Functions and Compile-Time](04-functions-and-compile-time.md)
