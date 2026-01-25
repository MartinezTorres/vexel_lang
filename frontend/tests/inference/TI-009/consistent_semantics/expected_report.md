## Stdout
```
// Lowered Vexel module: tests/inference/TI-009/consistent_semantics/test.vx
&increment(val) {
    val + 1
}
&^main() -> #i32 {
    a = increment_G_i8(10);
    b = increment_G_i8(20);
    c = increment_G_i8(30);
    a + b + c
}
&increment_G_i8(val: #i8) -> #i8 {
    val + 1
}
```

## Stderr
```
```

## Exit Code
0
