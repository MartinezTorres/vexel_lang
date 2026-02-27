## Stdout
```
// Lowered Vexel module: tests/expressions/EX-131/fixed_native64_muldiv/test.vx
&!ua() -> #u32.32;
&!ub() -> #u32.32;
&^main() -> #i32 {
    u = ua();
    v = ub();
    p = u * v;
    d = p / v;
    m = p % v;
    c = u;
    c *= v;
    c /= v;
    c %= v;
    ok = d == u && c == u && m < v;
    ok ? 0 : 1
}
```

## Stderr
```
```

## Exit Code
0
