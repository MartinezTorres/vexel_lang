## Stdout
```
// Lowered Vexel module: tests/inference/TI-014/nested_multiple_vars/test.vx
&^main() -> #i32 {
    x = 10;
    y = 20;
    &compute() -> #i32 {
        x + y
    }
    compute()
}
```

## Stderr
```
```

## Exit Code
0
