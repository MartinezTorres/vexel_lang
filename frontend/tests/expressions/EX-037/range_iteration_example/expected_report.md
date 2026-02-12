## Stdout
```
// Lowered Vexel module: tests/expressions/EX-037/range_iteration_example/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]@{
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
