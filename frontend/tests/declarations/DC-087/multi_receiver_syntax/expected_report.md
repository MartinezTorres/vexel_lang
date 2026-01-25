## Stdout
```
// Lowered Vexel module: tests/declarations/DC-087/multi_receiver_syntax/test.vx
&(a, b)swap() -> #i32 {
    a + b
}
&^main() -> #i32 {
    x = 10;
    y = 20;
    swap()
}
```

## Stderr
```
```

## Exit Code
0
