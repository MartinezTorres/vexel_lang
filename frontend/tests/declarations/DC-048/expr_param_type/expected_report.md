## Stdout
```
// Lowered Vexel module: tests/declarations/DC-048/expr_param_type/test.vx
&typed($expr: #i32) -> #i32 {
    expr
}
&^main() -> #i32 {
    typed(42)
}
```

## Stderr
```
```

## Exit Code
0
