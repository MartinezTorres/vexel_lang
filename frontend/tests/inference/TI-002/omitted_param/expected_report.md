## Stdout
```
// Lowered Vexel module: tests/inference/TI-002/omitted_param/test.vx
&first(a, b) {
    a
}
&^main() -> #i32 {
    first_G_i8_i8(10, 20)
}
&first_G_i8_i8(a: #i8, b: #i8) -> #i8 {
    a
}
```

## Stderr
```
```

## Exit Code
0
