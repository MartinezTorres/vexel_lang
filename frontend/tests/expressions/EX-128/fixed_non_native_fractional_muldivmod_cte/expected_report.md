## Stdout
```
// Lowered Vexel module: tests/expressions/EX-128/fixed_non_native_fractional_muldivmod_cte/test.vx
&!ua() -> #u40.32;
&!ub() -> #u40.32;
&^main() -> #i32 {
    u = ua();
    v = ub();
    prod = u * v;
    back = prod / v;
    rem = prod % v;
    acc = u;
    acc *= v;
    acc /= v;
    acc %= v;
    ok = back == u && acc == u && rem < v;
    ok ? 0 : 1
}
```

## Stderr
```
```

## Exit Code
0
