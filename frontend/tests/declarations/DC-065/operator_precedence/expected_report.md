## Stdout
```
// Lowered Vexel module: tests/declarations/DC-065/operator_precedence/test.vx
&^main() -> #i32 {
    a = Custom(1);
    b = Custom(2);
    c = Custom::+(b);
    c.data
}
```

## Stderr
```
```

## Exit Code
0
