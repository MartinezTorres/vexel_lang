## Stdout
```
// Lowered Vexel module: tests/grammar/GR-056/multi_receiver_simple/test.vx
&(a, b)swap() -> #i32 {
    a + b
}
&^main() -> #i32 {
    left: #i32 = 1;
    right: #i32 = 2;
    swap()
}
```

## Stderr
```
```

## Exit Code
0
