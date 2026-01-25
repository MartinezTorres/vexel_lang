## Stdout
```
// Lowered Vexel module: tests/grammar/GR-019/nested_ternary/test.vx
&nested(x: #i32) -> #i8 {
    result = x > 0 ? x > 10 ? 2 : 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
