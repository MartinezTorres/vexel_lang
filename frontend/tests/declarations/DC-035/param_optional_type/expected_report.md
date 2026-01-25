## Stdout
```
// Lowered Vexel module: tests/declarations/DC-035/param_optional_type/test.vx
&typed(x: #i32) -> #i32 {
    x
}
&inferred(y) {
    y
}
&^main() -> #i32 {
    typed(10) + inferred_G_i8(20)
}
&inferred_G_i8(y: #i8) -> #i8 {
    y
}
```

## Stderr
```
```

## Exit Code
0
