## Stdout
```
// Lowered Vexel module: tests/modules/NR-041/forward_ref_type/test.vx
&^main() -> #i32 {
    next = NextNode(2);
    node = Node(1, next);
    result = ( #i32 ) 0;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
