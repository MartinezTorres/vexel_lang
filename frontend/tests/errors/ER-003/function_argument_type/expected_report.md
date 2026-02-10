## Stdout
```
// Lowered Vexel module: tests/errors/ER-003/function_argument_type/test.vx
&^add(x: #i32, y: #i32) -> #i32 {
    x + y
}
&^main() -> #i32 {
    add(5, 1)
}
```

## Stderr
```
```

## Exit Code
0
