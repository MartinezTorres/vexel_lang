## Stdout
```
// Lowered Vexel module: tests/types/TY-050/type_constructor_no_self_reference/test.vx
#Simple(x: #i32, y: #i32);
&^main() -> #i32 {
    s = Simple(1, 2);
    s.x
}
```

## Stderr
```
```

## Exit Code
0
