## Stdout
```
// Lowered Vexel module: tests/expressions/EX-125/fixed_non_native_zero_frac_mul_div_mod_subset/test.vx
&^acc72_u(a: #u72.0, b: #u72.0, c: #u72.0) -> #u72.0 {
    x = a;
    x = x * b;
    x = x / c;
    x = x % b;
    x *= b;
    x /= c;
    x %= b;
    x
}
&^acc72_s(a: #i72.0, b: #i72.0, c: #i72.0) -> #i72.0 {
    x = a;
    x = x * b;
    x = x / c;
    x = x % b;
    x *= b;
    x /= c;
    x %= b;
    x
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
