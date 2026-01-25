## Stdout
```
// Lowered Vexel module: tests/errors/ER-012/i8_wrap_negative/test.vx
&^main() -> #i32 {
    x = ( #i8 ) -128;
    y = ( #i8 ) x - 1;
    y
}
```

## Stderr
```
```

## Exit Code
0
