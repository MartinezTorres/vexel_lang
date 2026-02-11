## Stdout
```
// Lowered Vexel module: tests/expressions/EX-034/stable_sort/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr: #i32[6] = [3, 1, 2, 1, 3, 2];
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
