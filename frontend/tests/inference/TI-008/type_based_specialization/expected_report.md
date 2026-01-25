## Stdout
```
// Lowered Vexel module: tests/inference/TI-008/type_based_specialization/test.vx
&process(data) {
    data
}
&^main() -> #i32 {
    x = process_G_i8(42);
    y = process_G_i8(100);
    x + y
}
&process_G_i8(data: #i8) -> #i8 {
    data
}
```

## Stderr
```
```

## Exit Code
0
