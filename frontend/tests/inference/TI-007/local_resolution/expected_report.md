## Stdout
```
// Lowered Vexel module: tests/inference/TI-007/local_resolution/test.vx
&helper(x) {
    x + 1
}
&compute(a, b) {
    helper(a) + helper(b)
}
&^main() -> #i32 {
    compute_G_i8_i8(10, 20)
}
&helper_G_i8(x: #i8) -> #i8 {
    x + 1
}
&compute_G_i8_i8(a: #i8, b: #i8) -> #i8 {
    helper_G_i8(a) + helper_G_i8(b)
}
```

## Stderr
```
```

## Exit Code
0
