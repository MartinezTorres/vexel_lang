## Stdout
```
// Lowered Vexel module: tests/errors/ER-002/duplicate_binding/test.vx
&^main() -> #i32 {
    x: #i32 = 5;
    x = 10;
    x
}
```

## Stderr
```
```

## Exit Code
0
