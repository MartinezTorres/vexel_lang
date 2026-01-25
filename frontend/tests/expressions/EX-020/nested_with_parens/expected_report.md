## Stdout
```
// Lowered Vexel module: tests/expressions/EX-020/nested_with_parens/test.vx
&^main() -> #i32 {
    a = 1;
    b = 0;
    c = 1;
    result1 = a ? ( #i32 ) 1 : b ? ( #i32 ) 2 : ( #i32 ) 3;
    result2 = a ? b : c ? ( #i32 ) 2 : ( #i32 ) 3;
    -> result1;
}
```

## Stderr
```
```

## Exit Code
0
