## Stdout
```
// Lowered Vexel module: tests/expressions/EX-088/assignment_associativity/test.vx
&^main() -> #i32 {
    a = 0;
    b = 0;
    c = 42;
    a = b = c;
    -> a + b;
}
```

## Stderr
```
```

## Exit Code
0
