## Stdout
```
// Lowered Vexel module: tests/expressions/EX-122/fixed_non_native_zero_frac_bitwise_shift_subset/test.vx
&^bitmix72(a: #u72.0, b: #u72.0, s: #u72.0) -> #u72.0 {
    x = a;
    x &= b;
    x |= a;
    x ^= b;
    x <<= s;
    x >>= s;
    ~a & x
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
