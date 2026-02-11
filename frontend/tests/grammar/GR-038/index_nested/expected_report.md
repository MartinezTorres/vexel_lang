## Stdout
```
// Lowered Vexel module: tests/grammar/GR-038/index_nested/test.vx
&^main() -> #i32 {
    matrix: #i32[2] = [1, 2];
    arr: #i32[3] = [3, 4, 5];
    x = matrix[0];
    y = arr[1];
    x + y
}
```

## Stderr
```
```

## Exit Code
0
