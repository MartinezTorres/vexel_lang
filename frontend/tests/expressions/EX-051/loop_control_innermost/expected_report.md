## Stdout
```
// Lowered Vexel module: tests/expressions/EX-051/loop_control_innermost/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    outer: #i32 = 0;
    outer < 3@{
            inner: #i32 = 0;
            inner < 3@{
                    inner == 1 ? 
                        ->|;
                    print(inner);
                    inner = inner + 1
                };
            outer = outer + 1
        };
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
