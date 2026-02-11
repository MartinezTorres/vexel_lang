## Stdout
```
// Lowered Vexel module: tests/expressions/EX-035/underscore_scoped/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr: #i32[3] = [1, 2, 3];
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
