## Stdout
```
// Lowered Vexel module: tests/expressions/EX-095/logical_and_tighter_than_or/test.vx
&^main() -> #i32 {
    result = 0 || 1 && 0;
    result ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
