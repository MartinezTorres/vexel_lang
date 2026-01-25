## Stdout
```
// Lowered Vexel module: tests/grammar/GR-016/nested_decl/test.vx
&outer() -> #T0 {
    #Inner(val: #i32);
    &helper() {
        -> 10;
    }
    x = helper()
}
```

## Stderr
```
```

## Exit Code
0
