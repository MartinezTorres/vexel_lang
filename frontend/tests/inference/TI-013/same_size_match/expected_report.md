## Stdout
```
// Lowered Vexel module: tests/inference/TI-013/same_size_match/test.vx
&^main() -> #i32 {
    a = [1, 2, 3, 4, 5];
    b = [6, 7, 8, 9, 10];
    b = a;
    b[0]
}
```

## Stderr
```
```

## Exit Code
0
