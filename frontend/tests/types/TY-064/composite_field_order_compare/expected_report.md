## Stdout
```
// Lowered Vexel module: tests/types/TY-064/composite_field_order_compare/test.vx
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
