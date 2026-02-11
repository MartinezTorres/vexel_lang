## Stdout
```
// Lowered Vexel module: tests/expressions/EX-031/sorted_iteration_comparison/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr: #i32[5] = [5, 2, 8, 1, 9];
    arr@@{
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
