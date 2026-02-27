## Stdout
```
// Lowered Vexel module: tests/declarations/DC-089/receiver_mutable_alias/test.vx
&^main() -> #i32 {
    x = 5;
    y = 10;
    (x, y).increment_both_G_i32_i32();
    x + y
}
&(p, q)increment_both_G_i32_i32() -> #i32 {
    p = p + 1;
    q = q + 1
}
```

## Stderr
```
```

## Exit Code
0
