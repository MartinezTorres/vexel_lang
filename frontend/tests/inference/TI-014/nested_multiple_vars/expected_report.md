## Stdout
```
// Lowered Vexel module: tests/inference/TI-014/nested_multiple_vars/test.vx
&^main() -> #i32 {
    x: #i32 = 10;
    y: #i32 = 20;
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
