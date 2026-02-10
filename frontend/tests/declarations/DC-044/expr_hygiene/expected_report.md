## Stdout
```
// Lowered Vexel module: tests/declarations/DC-044/expr_hygiene/test.vx
&^main() -> #i32 {
    x = 10;
    test(x = x + 1);
    x
}
```

## Stderr
```
```

## Exit Code
0
