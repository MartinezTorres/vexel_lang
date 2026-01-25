## Stdout
```
// Lowered Vexel module: tests/declarations/DC-091/receiver_type_infer/test.vx
&(m, n)multiply() -> #i32 {
    m * n
}
&^main() -> #i32 {
    x = 6;
    y = 7;
    multiply()
}
```

## Stderr
```
```

## Exit Code
0
