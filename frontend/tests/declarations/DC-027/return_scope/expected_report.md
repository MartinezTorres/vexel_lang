## Stdout
```
// Lowered Vexel module: tests/declarations/DC-027/return_scope/test.vx
&outer() -> #i32 {
    &inner() -> #i32 {
        -> 10;
    }
    10 + 20
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
