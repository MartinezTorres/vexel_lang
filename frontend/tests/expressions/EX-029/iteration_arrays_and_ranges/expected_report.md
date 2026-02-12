## Stdout
```
// Lowered Vexel module: tests/expressions/EX-029/iteration_arrays_and_ranges/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr: #i32[3] = [10, 20, 30];
    arr@{
            print(_);
        };
    [0, 1, 2, 3, 4]@{
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
