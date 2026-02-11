## Stdout
```
// Lowered Vexel module: tests/expressions/EX-012/block_yields_last_expr/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    result: #i32 = {
        print(1);
        print(2);
        print(3);
        42
    };
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
