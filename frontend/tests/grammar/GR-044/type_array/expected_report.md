## Stdout
```
// Lowered Vexel module: tests/grammar/GR-044/type_array/test.vx
&^main() -> #i32 {
    arr = [1, 2, 3];
    matrix = [1.0, 2.0];
    arr[0] + ( #i32 ) matrix[0]
}
```

## Stderr
```
```

## Exit Code
0
