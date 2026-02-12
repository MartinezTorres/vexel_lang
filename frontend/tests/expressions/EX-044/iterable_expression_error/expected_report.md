## Stdout
```
// Lowered Vexel module: tests/expressions/EX-044/iterable_expression_error/test.vx
&print(arg0: #i32) {
    0
}
&^main() -> #i32 {
    [1, 2, 3]@{
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
