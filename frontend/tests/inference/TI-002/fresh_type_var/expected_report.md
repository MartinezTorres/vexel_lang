## Stdout
```
// Lowered Vexel module: tests/inference/TI-002/fresh_type_var/test.vx
&swap(a, b) {
    [b, a]
}
&^main() -> #i32 {
    swap_G_b_i8(1, 2)[0]
}
&swap_G_b_i8(a: #b, b: #i8) -> #i8[2] {
    [b, a]
}
```

## Stderr
```
```

## Exit Code
0
