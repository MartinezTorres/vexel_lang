## Stdout
```
// Lowered Vexel module: tests/expressions/EX-088/assignment_associativity/test.vx
&^main() -> #i32 {
    a: #i32 = 0;
    b: #i32 = 0;
    c: #i32 = 42;
    a = b = c;
    -> a + b;
}
```

## Stderr
```
```

## Exit Code
0
