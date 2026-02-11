## Stdout
```
// Lowered Vexel module: tests/grammar/GR-035/multi_receiver_call/test.vx
&(x, y)swap_with(delta: #i32) -> #i32 {
    x + y + delta
}
&^main() -> #i32 {
    a: #i32 = 1;
    b: #i32 = 2;
    swap_with(3)
}
```

## Stderr
```
```

## Exit Code
0
