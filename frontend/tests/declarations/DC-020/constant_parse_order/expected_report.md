## Stdout
```
// Lowered Vexel module: tests/declarations/DC-020/constant_parse_order/test.vx
A: #i8 = 2;
B: #i8 = A + 2;
C: #i8 = B + 2;
&^main() -> #i32 {
    C
}
```

## Stderr
```
```

## Exit Code
0
