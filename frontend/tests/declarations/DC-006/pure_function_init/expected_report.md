## Stdout
```
// Lowered Vexel module: tests/declarations/DC-006/pure_function_init/test.vx
&square(x: #i32) -> #i32 {
    x * x
}
SQUARED: #i32 = square(10);
&^main() -> #i32 {
    SQUARED
}
```

## Stderr
```
```

## Exit Code
0
