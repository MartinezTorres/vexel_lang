## Stdout
```
// Lowered Vexel module: tests/inference/TI-003/array_element_type/test.vx
&sum_array(arr) {
    arr[0] + arr[1] + arr[2]
}
&^main() -> #i32 {
    sum_array_G_array([10, 20, 30])
}
&sum_array_G_array(arr: #i8[3]) -> #i8 {
    arr[0] + arr[1] + arr[2]
}
```

## Stderr
```
```

## Exit Code
0
