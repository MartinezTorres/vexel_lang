## Stdout
```
// Lowered Vexel module: tests/inference/TI-004/multiple_array_sizes/test.vx
&count_one(arr: #i32[1]) -> #i32 {
    |arr|
}
&count_many(arr: #i32[4]) -> #i32 {
    |arr|
}
&^main() -> #i32 {
    count_one([1]) + count_many([1, 2, 3, 4])
}
```

## Stderr
```
```

## Exit Code
0
