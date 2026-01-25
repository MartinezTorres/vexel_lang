## Stdout
```
// Lowered Vexel module: tests/grammar/GR-034/func_call_args/test.vx
&calculate(a: #i32, b: #i32, c: #i32) -> #i32 {
    a + b + c
}
&^main() -> #i32 {
    result = calculate(10, 20, 30);
    result
}
```

## Stderr
```
```

## Exit Code
0
