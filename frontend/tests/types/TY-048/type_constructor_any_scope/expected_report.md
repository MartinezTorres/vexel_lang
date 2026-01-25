## Stdout
```
// Lowered Vexel module: tests/types/TY-048/type_constructor_any_scope/test.vx
#Global(x: #i32);
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
