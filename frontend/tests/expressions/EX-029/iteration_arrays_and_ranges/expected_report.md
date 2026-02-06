## Stdout
```
// Lowered Vexel module: tests/expressions/EX-029/iteration_arrays_and_ranges/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr = [10, 20, 30];
    arr@{
        print(_);
    };
    0..5@{
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
