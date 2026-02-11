## Stdout
```
// Lowered Vexel module: tests/declarations/DC-045/fresh_evaluation/test.vx
&use_twice($expr: #T0) -> #i32 {
    a: #i32 = expr;
    b: #i32 = expr;
    a + b
}
&^main() -> #i32 {
    count: #i32 = 0;
    result = use_twice(count = count + 1);
    result
}
```

## Stderr
```
```

## Exit Code
0
