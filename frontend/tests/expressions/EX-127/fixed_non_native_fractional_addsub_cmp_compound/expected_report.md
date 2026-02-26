## Stdout
```
// Lowered Vexel module: tests/expressions/EX-127/fixed_non_native_fractional_addsub_cmp_compound/test.vx
!!a: #i40.32;
!!b: #i40.32;
!!u: #u40.32;
!!v: #u40.32;
&^main() -> #i32 {
    xs = -a + b - a;
    ok1 = xs >= a - b || xs < a + b;
    t = u;
    t += v;
    t -= u;
    ok2 = t == v;
    ok1 && ok2 ? 0 : 1
}
```

## Stderr
```
```

## Exit Code
0
