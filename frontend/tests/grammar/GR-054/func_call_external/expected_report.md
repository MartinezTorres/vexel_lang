## Stdout
```
// Lowered Vexel module: tests/grammar/GR-054/func_call_external/test.vx
&!printf(fmt: #s, val: #i32) -> #i32;
&^main() -> #i32 {
    printf("value: %d
", 42);
    0
}
```

## Stderr
```
```

## Exit Code
0
