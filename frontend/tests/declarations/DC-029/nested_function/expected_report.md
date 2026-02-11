## Stdout
```
// Lowered Vexel module: tests/declarations/DC-029/nested_function/test.vx
&outer() -> #i32 {
    &inner() -> #i32 {
        42
    }
    42
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
