## Stdout
```
// Lowered Vexel module: tests/types/TY-064/composite_field_order_compare/test.vx
#Triple(a: #i32, b: #i32, c: #i32);
&^main() -> #i32 {
    t1 = Triple(1, 2, 3);
    t2 = Triple(1, 2, 4);
    t1 < t2 ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
