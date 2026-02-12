## Stdout
```
// Lowered Vexel module: tests/types/TY-051/composite_compare_order/test.vx
#Pair(a: #i32, b: #i32);
&^main() -> #i32 {
    p1 = Pair(1, 2);
    p2 = Pair(1, 3);
    p1 < p2 ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
