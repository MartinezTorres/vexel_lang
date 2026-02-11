## Stdout
```
// Lowered Vexel module: tests/declarations/DC-093/tuple_call/test.vx
&(r1, r2)add_assign(val: #i32) -> #T1 {
    r1 = r1 + val;
    r2 = r2 + val
}
&^main() -> #i32 {
    x: #i32 = 1;
    y: #i32 = 2;
    add_assign(10);
    x + y
}
```

## Stderr
```
```

## Exit Code
0
