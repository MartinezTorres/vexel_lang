## Stdout
```
// Lowered Vexel module: tests/inference/TI-008/pure_semantics/test.vx
&pure_func(x) {
    x + x
}
&^main() -> #i32 {
    pure_func_G_i8(5) + pure_func_G_i8(10)
}
&pure_func_G_i8(x: #i8) -> #i8 {
    x + x
}
```

## Stderr
```
```

## Exit Code
0
