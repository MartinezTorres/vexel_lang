## Stdout
```
// Lowered Vexel module: tests/grammar/GR-054/func_call_internal/test.vx
&helper() -> #i32 {
    -> 42;
}
&main() -> #i32 {
    x = helper();
    y = helper() + helper()
}
```

## Stderr
```
```

## Exit Code
0
