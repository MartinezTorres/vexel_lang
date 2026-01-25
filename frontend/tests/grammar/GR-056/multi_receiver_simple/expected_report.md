## Stdout
```
// Lowered Vexel module: tests/grammar/GR-056/multi_receiver_simple/test.vx
&(a, b)swap() -> #i32 {
    a + b
}
&(x, y, z)transform(delta: #i32) -> #i32 {
    x + y + z + delta
}
&^main() -> #i32 {
    left = 1;
    right = 2;
    swap()
}
```

## Stderr
```
```

## Exit Code
0
