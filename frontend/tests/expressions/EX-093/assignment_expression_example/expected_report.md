## Stdout
```
// Lowered Vexel module: tests/expressions/EX-093/assignment_expression_example/test.vx
&^main() -> #i32 {
    x = 0;
    y = (x = 5) + 1;
    -> x * 10 + y;
}
```

## Stderr
```
```

## Exit Code
0
