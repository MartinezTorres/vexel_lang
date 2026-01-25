## Stdout
```
// Lowered Vexel module: tests/inference/TI-012/dead_branch_with_error/test.vx
&^main() -> #i32 {
    false ? undefined_variable : 100
}
```

## Stderr
```
```

## Exit Code
0
