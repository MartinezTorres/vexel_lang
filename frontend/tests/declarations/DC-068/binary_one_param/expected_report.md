## Stdout
```
// Lowered Vexel module: tests/declarations/DC-068/binary_one_param/test.vx
#BinOp(x: #i32);
&(a)#BinOp::+(b: #BinOp) -> #BinOp {
    BinOp(a.x + b.x)
}
&^main() -> #i32 {
    0
}
```

## Stderr
```
```

## Exit Code
0
