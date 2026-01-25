## Stdout
```
// Lowered Vexel module: tests/inference/TI-001/multiple_calls/test.vx
&add(a, b) {
    a + b
}
&^main() -> #i32 {
    add_G_i8_i8(10, 20) + add_G_i8_i8(5, 7)
}
&add_G_i8_i8(a: #i8, b: #i8) -> #i8 {
    a + b
}
```

## Stderr
```
```

## Exit Code
0
