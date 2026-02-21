## Stdout
```
// Lowered Vexel module: tests/expressions/EX-085/assignment_returns_value/test.vx
&^main() -> #i32 {
    x = 0;
    y = 0;
    y = x = 42;
    -> y;
}
```

## Stderr
```
```

## Exit Code
0
