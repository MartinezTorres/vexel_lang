## Stdout
```
// Lowered Vexel module: tests/expressions/EX-060/length_in_iteration/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr = [10, 20, 30, 40];
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
