## Stdout
```
// Lowered Vexel module: tests/grammar/GR-020/multiple_or/test.vx
&^main() -> #i32 {
    result = 0 || 0 || 1 || 0;
    result ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
