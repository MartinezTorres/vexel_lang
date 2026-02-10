## Stdout
```
// Lowered Vexel module: tests/types/TY-048/type_constructor_any_scope/test.vx
&^test() -> #i32 {
    #Local(y: #i32);
    l = Local(42);
    l.y
}
&^main() -> #i32 {
    test()
}
```

## Stderr
```
```

## Exit Code
0
