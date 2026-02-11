## Stdout
```
// Lowered Vexel module: tests/declarations/DC-088/receiver_eval_order/test.vx
&(x, y)combine() -> #i32 {
    x + y
}
&^main() -> #i32 {
    a: #i32 = 1;
    b: #i32 = 2;
    combine()
}
```

## Stderr
```
```

## Exit Code
0
