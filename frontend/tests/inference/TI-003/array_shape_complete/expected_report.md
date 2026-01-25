## Stdout
```
// Lowered Vexel module: tests/inference/TI-003/array_shape_complete/test.vx
&process(arr) {
    arr[0] * 2 + arr[1] * 3
}
&^main() -> #i32 {
    process_G_array([5, 7])
}
&process_G_array(arr: #i8[2]) -> #i8 {
    arr[0] * 2 + arr[1] * 3
}
```

## Stderr
```
```

## Exit Code
0
