## Stdout
```
// Lowered Vexel module: tests/inference/TI-002/call_site_inference/test.vx
&max(a, b) {
    a > b ? a : b
}
&^main() -> #i32 {
    max_G_i8_i16(100, 200)
}
&max_G_i8_i16(a: #i8, b: #i16) -> #i16 {
    a > b ? a : b
}
```

## Stderr
```
```

## Exit Code
0
