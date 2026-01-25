## Stdout
```
// Lowered Vexel module: tests/inference/TI-004/length_specialization/test.vx
&sum_pair(values: #i32[2]) -> #i32 {
    values[0] + values[1]
}
&sum_triple(values: #i32[3]) -> #i32 {
    values[0] + values[1] + values[2]
}
&^main() -> #i32 {
    sum_pair([1, 2]) + sum_triple([3, 4, 5])
}
```

## Stderr
```
```

## Exit Code
0
