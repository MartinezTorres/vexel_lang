## Stdout
```
// Lowered Vexel module: tests/declarations/DC-087/multi_receiver_syntax/test.vx
&(a, b)swap() -> #i32 {
    a + b
}
&^main() -> #i32 {
    x: #i32 = 10;
    y: #i32 = 20;
    swap()
}
```

## Stderr
```
```

## Exit Code
0
