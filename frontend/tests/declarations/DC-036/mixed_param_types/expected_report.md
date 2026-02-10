## Stdout
```
// Lowered Vexel module: tests/declarations/DC-036/mixed_param_types/test.vx
&mixed(a: #i32, b, c: #i32, d) -> #i32 {
    a + b + c + d
}
&^main() -> #i32 {
    mixed_G_i8_i8_i8_i8(2, 3, 4, 5)
}
&mixed_G_i8_i8_i8_i8(a: #i8, b: #i8, c: #i8, d: #i8) -> #i32 {
    a + b + c + d
}
```

## Stderr
```
```

## Exit Code
0
