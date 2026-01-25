## Stdout
```
// Lowered Vexel module: tests/expressions/EX-018/compile_time_default/test.vx
A: #i32 = 10;
B: #i32 = 20;
&^main() -> #i32 {
    result = A > B ? A : B;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
