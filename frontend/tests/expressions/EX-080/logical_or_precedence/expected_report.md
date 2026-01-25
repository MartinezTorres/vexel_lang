## Stdout
```
// Lowered Vexel module: tests/expressions/EX-080/logical_or_precedence/test.vx
&^main() -> #i32 {
    result = false && true || true;
    result ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
