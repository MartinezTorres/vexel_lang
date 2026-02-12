## Stdout
```
// Lowered Vexel module: tests/expressions/EX-055/length_absolute_syntax/test.vx
&^main() -> #i32 {
    arr: #i32[5] = [1, 2, 3, 4, 5];
    len = 5;
    x: #i32 = -10;
    abs = |x|;
    -> len + abs;
}
```

## Stderr
```
```

## Exit Code
0
