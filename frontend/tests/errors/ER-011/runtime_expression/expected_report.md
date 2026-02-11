## Stdout
```
// Lowered Vexel module: tests/errors/ER-011/runtime_expression/test.vx
&^compute_zero() -> #i32 {
    0
}
&^main() -> #i32 {
    10 / 0
}
```

## Stderr
```
```

## Exit Code
0
