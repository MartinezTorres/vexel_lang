## Stdout
```
// Lowered Vexel module: tests/expressions/EX-001/function_call_syntax/test.vx
&!external_add(a: #i32, b: #i32) -> #i32;
&add(a: #i32, b: #i32) -> #i32 {
    -> a + b;
}
&^main() -> #i32 {
    x = add(3, 4);
    y = external_add(5, 6);
    -> x + y;
}
```

## Stderr
```
```

## Exit Code
0
