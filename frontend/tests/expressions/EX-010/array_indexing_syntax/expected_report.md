## Stdout
```
// Lowered Vexel module: tests/expressions/EX-010/array_indexing_syntax/test.vx
&^main() -> #i32 {
    arr: #i32[5] = [1, 2, 3, 4, 5];
    x = arr[0];
    y = arr[4];
    -> x + y;
}
```

## Stderr
```
```

## Exit Code
0
