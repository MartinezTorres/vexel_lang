## Stdout
```
// Lowered Vexel module: tests/declarations/DC-044/expr_hygiene/test.vx
&test($expr: #T0) -> #T0 {
    x: #i32 = 999;
    expr
}
&^main() -> #i32 {
    x: #i32 = 10;
    test(x = x + 1);
    x
}
```

## Stderr
```
```

## Exit Code
0
