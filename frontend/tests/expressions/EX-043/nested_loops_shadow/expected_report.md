## Stdout
```
// Lowered Vexel module: tests/expressions/EX-043/nested_loops_shadow/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    outer = [10, 20];
    outer@{
            inner = [1, 2];
            inner@{
                    print(_)
                };
        };
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
