## Stdout
```
// Lowered Vexel module: tests/declarations/DC-091/receiver_type_infer/test.vx
&(m, n)multiply() -> #i32 {
    m * n
}
&^main() -> #i32 {
    x: #i32 = 6;
    y: #i32 = 7;
    multiply()
}
```

## Stderr
```
```

## Exit Code
0
