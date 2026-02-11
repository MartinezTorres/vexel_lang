## Stdout
```
// Lowered Vexel module: tests/declarations/DC-040/expr_param_syntax/test.vx
&apply_twice($expr: #T0) -> #T0 {
    expr;
    expr
}
&^main() -> #i32 {
    x: #i32 = 0;
    apply_twice(x = x + 1);
    x
}
```

## Stderr
```
```

## Exit Code
0
