## Stdout
```
// Lowered Vexel module: tests/expressions/EX-030/sorted_iteration_copies/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr: #i32[3] = [30, 10, 20];
    arr@@{
            print(_);
        };
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
