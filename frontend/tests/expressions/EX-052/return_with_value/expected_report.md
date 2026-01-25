## Stdout
```
// Lowered Vexel module: tests/expressions/EX-052/return_with_value/test.vx
&add(a: #i32, b: #i32) -> #i32 {
    -> a + b;
}
&^main() -> #i32 {
    result = add(10, 20);
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
