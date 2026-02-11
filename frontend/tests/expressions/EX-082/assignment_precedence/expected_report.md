## Stdout
```
// Lowered Vexel module: tests/expressions/EX-082/assignment_precedence/test.vx
&^main() -> #i32 {
    x: #i32 = 0;
    y: #i32 = 0;
    y = x = 10;
    y
}
```

## Stderr
```
```

## Exit Code
0
