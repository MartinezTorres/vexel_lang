## Stdout
```
// Lowered Vexel module: tests/expressions/EX-097/nested_conditionals_with_parens/test.vx
&^main() -> #i32 {
    result1 = 1 < 2 ? 3 : 0 < -1 ? 5 : 6;
    result2 = 1 < 2 ? 3 < 4 : 0 > 1 ? 5 : 6;
    -> result1;
}
```

## Stderr
```
```

## Exit Code
0
