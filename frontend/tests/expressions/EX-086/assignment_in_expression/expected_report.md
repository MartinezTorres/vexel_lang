## Stdout
```
// Lowered Vexel module: tests/expressions/EX-086/assignment_in_expression/test.vx
&^main() -> #i32 {
    x = 0;
    y = x = 5 + 1;
    -> y;
}
```

## Stderr
```
```

## Exit Code
0
