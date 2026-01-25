## Stdout
```
// Lowered Vexel module: tests/inference/TI-001/return_inference/test.vx
&double(x) {
    x * 2
}
&^main() -> #i32 {
    double_G_i8(21)
}
&double_G_i8(x: #i8) -> #i8 {
    x * 2
}
```

## Stderr
```
```

## Exit Code
0
