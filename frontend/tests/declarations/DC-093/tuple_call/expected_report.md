## Stdout
```
// Lowered Vexel module: tests/declarations/DC-093/tuple_call/test.vx
&^main() -> #i32 {
    x = 1;
    y = 2;
    (x, y).add_assign_G_i32_i32_i32(10);
    x + y
}
&(r1, r2)add_assign_G_i32_i32_i32(val: #i32) -> #i32 {
    r1 = r1 + val;
    r2 = r2 + val
}
```

## Stderr
```
```

## Exit Code
0
