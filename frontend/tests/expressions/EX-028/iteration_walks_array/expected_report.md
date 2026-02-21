## Stdout
```
// Lowered Vexel module: tests/expressions/EX-028/iteration_walks_array/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr = [1, 2, 3, 4, 5];
    arr@{
            print(_);
        };
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
