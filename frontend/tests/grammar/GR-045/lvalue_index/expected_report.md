## Stdout
```
// Lowered Vexel module: tests/grammar/GR-045/lvalue_index/test.vx
&^main() -> #i32 {
    mut arr: #i32[2] = [0, 0];
    arr[0] = 10;
    arr[0]
}
```

## Stderr
```
```

## Exit Code
0
