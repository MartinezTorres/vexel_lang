## Stdout
```
// Lowered Vexel module: tests/declarations/DC-042/expr_substitution/test.vx
&triple($expr: #T0) -> #T0 {
    expr;
    expr;
    expr
}
&^main() -> #i32 {
    count: #i32 = 0;
    triple(count = count + 1);
    count
}
```

## Stderr
```
```

## Exit Code
0
