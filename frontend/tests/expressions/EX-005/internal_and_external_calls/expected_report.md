## Stdout
```
// Lowered Vexel module: tests/expressions/EX-005/internal_and_external_calls/test.vx
&!external_multiply(arg0: #i32, arg1: #i32) -> #i32;
&^main() -> #i32 {
    sum = 30;
    product = external_multiply(3, 4);
    -> sum + product;
}
```

## Stderr
```
```

## Exit Code
0
