## Stdout
```
// Lowered Vexel module: tests/types/TY-049/type_constructor_no_recursion/test.vx
#Node(value: #i32);
&^main() -> #i32 {
    n = Node(42);
    n.value
}
```

## Stderr
```
```

## Exit Code
0
