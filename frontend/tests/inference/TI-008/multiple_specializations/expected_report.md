## Stdout
```
// Lowered Vexel module: tests/inference/TI-008/multiple_specializations/test.vx
&combine(a, b) {
    a + b
}
&^main() -> #i32 {
    combine_G_i8_i8(1, 2) + combine_G_i8_i8(10, 20) + combine_G_i8_i16(100, 200)
}
&combine_G_i8_i8(a: #i8, b: #i8) -> #i8 {
    a + b
}
&combine_G_i8_i16(a: #i8, b: #i16) -> #i16 {
    a + b
}
```

## Stderr
```
```

## Exit Code
0
