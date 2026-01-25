## Stdout
```
// Lowered Vexel module: tests/grammar/GR-020/multiple_or/test.vx
&^main() -> #i32 {
    result = false || false || true || false;
    result ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
