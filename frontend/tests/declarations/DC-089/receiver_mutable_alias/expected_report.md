## Stdout
```
// Lowered Vexel module: tests/declarations/DC-089/receiver_mutable_alias/test.vx
&(p, q)increment_both() -> #T1 {
    p = p + 1;
    q = q + 1
}
&^main() -> #i32 {
    x: #i32 = 5;
    y: #i32 = 10;
    increment_both();
    x + y
}
```

## Stderr
```
```

## Exit Code
0
