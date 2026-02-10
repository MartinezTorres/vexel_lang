## Stdout
```
// Lowered Vexel module: tests/expressions/EX-044/iterable_expression_error/test.vx
&print(arg0: #i32) {
}
&get_array() -> #i32[3] {
    -> [1, 2, 3];
}
&^main() -> #i32 {
    get_array()@{
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
