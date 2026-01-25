## Stdout
```
// Lowered Vexel module: tests/expressions/EX-082/assignment_precedence/test.vx
&^main() -> #i32 {
    x = 0;
    y = 0;
    y = x = true ? 10 : 20;
    y
}
```

## Stderr
```
```

## Exit Code
0
