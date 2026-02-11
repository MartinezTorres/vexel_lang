## Stdout
```
// Lowered Vexel module: tests/types/TY-017/primitive_by_value/test.vx
&^copy(x: #i32) -> #i32 {
    x
}
&^main() -> #i32 {
    a: #i32 = 42;
    b = copy(a);
    b
}
```

## Stderr
```
```

## Exit Code
0
