## Stdout
```
// Lowered Vexel module: tests/modules/NR-041/forward_ref_type/test.vx
#Node(value: #i32, next: #NextNode);
#NextNode(data: #i32);
&^main() -> #i32 {
    next = NextNode(2);
    node = Node(1, next);
    result = node.value == 1 && node.next.data == 2 ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
