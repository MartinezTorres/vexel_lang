## Stdout
```
// Lowered Vexel module: tests/expressions/EX-038/array_index_iteration/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr: #i32[4] = [100, 200, 300, 400];
    [0, 1, 2, 3]@{
            print(arr[_]);
        };
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
