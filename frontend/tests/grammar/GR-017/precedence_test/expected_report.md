## Stdout
```
// Lowered Vexel module: tests/grammar/GR-017/precedence_test/test.vx
&^main() -> #i32 {
    x: #i32 = 1 + 2 * 3;
    y = 0 || 1 && 0;
    z = 1 < 2 == 3 > 4;
    x
}
```

## Stderr
```
```

## Exit Code
0
