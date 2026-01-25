## Stdout
```
// Lowered Vexel module: tests/expressions/EX-079/logical_and_precedence/test.vx
&^main() -> #i32 {
    result = 5 == 5 && 3 < 4;
    -> result ? 1 : 0;
}
```

## Stderr
```
```

## Exit Code
0
