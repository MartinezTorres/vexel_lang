## Stdout
```
// Lowered Vexel module: tests/inference/TI-004/different_lengths/test.vx
&first_elem(arr) {
    arr[0]
}
&^main() -> #i32 {
    first_elem_G_array([1, 2, 3]) + first_elem_G_array([4, 5])
}
&first_elem_G_array(arr: #i8[3]) -> #i8 {
    arr[0]
}
```

## Stderr
```
```

## Exit Code
0
