## Stdout
```
// Lowered Vexel module: tests/errors/ER-008/nested_conditional_fixed/test.vx
&^main() -> #i32 {
    choice = true ? 1 : false ? 2 : 3;
    choice
}
```

## Stderr
```
```

## Exit Code
0
