## Stdout
```
// Lowered Vexel module: tests/declarations/DC-020/constant_parse_order/test.vx
A: #i8 = 1;
B: #i8 = A + 1;
C: #i8 = B + 1;
&^main() -> #i32 {
    C
}
```

## Stderr
```
```

## Exit Code
0
