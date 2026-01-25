## Stdout
```
// Lowered Vexel module: tests/inference/TI-007/internal_generic/test.vx
&wrap(value) {
    [value]
}
&unwrap(wrapped) {
    wrapped[0]
}
&^main() -> #i32 {
    unwrap_G_array(wrap_G_i8(42))
}
&wrap_G_i8(value: #i8) -> #i8[1] {
    [value]
}
&unwrap_G_array(wrapped: #i8[1]) -> #i8 {
    wrapped[0]
}
```

## Stderr
```
```

## Exit Code
0
