## Stdout
```
// Lowered Vexel module: tests/inference/TI-013/array_size_distinct/test.vx
&!input() -> #i32;
&^main() -> #i32 {
    a = [1, 2, 3];
    b = [1, 2, 3, 4, 5];
    a[0] = input();
    b[0] = input();
    process_G_array_i32_n3(a) + process_G_array_i32_n5(b)
}
&process_G_array_i32_n3(arr: #i32[3]) -> #i32 {
    arr[0]
}
&process_G_array_i32_n5(arr: #i32[5]) -> #i32 {
    arr[0]
}
```

## Stderr
```
```

## Exit Code
0
