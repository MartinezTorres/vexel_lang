## Stdout
```
// Lowered Vexel module: tests/expressions/EX-058/absolute_value_signed/test.vx
&^main() -> #i32 {
    x: #i32 = -42;
    abs_x = |x|;
    y: #i32 = 15;
    abs_y = |y|;
    -> abs_x + abs_y;
}
```

## Stderr
```
```

## Exit Code
0
