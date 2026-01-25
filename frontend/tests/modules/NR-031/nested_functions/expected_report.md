## Stdout
```
// Lowered Vexel module: tests/modules/NR-031/nested_functions/test.vx
&outer() -> #i32 {
    &inner() -> #i32 {
        99
    }
    inner()
}
&^main() -> #i32 {
    outer()
}
```

## Stderr
```
```

## Exit Code
0
