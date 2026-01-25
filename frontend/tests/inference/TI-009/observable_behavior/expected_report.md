## Stdout
```
// Lowered Vexel module: tests/inference/TI-009/observable_behavior/test.vx
&compute(x, y) {
    x * 2 + y * 3
}
&^main() -> #i32 {
    compute_G_i8_i8(5, 7)
}
&compute_G_i8_i8(x: #i8, y: #i8) -> #i8 {
    x * 2 + y * 3
}
```

## Stderr
```
```

## Exit Code
0
