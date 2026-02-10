## Stdout
```
// Lowered Vexel module: tests/inference/TI-013/array_size_distinct/test.vx
&^main() -> #i32 {
    a = [1, 2, 3];
    b = [1, 2, 3, 4, 5];
    process_G_array(a) + process_G_array(b)
}
```

## Stderr
```
```

## Exit Code
0
