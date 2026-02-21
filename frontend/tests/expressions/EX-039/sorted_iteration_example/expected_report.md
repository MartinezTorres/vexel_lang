## Stdout
```
// Lowered Vexel module: tests/expressions/EX-039/sorted_iteration_example/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr = [40, 10, 30, 20];
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
