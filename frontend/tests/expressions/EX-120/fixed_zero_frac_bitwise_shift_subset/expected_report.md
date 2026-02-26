## Stdout
```
// Lowered Vexel module: tests/expressions/EX-120/fixed_zero_frac_bitwise_shift_subset/test.vx
&^bit_and_u(a: #u8.0, b: #u8.0) -> #u8.0 {
    a & b
}
&^bit_or_u(a: #u8.0, b: #u8.0) -> #u8.0 {
    a | b
}
&^bit_xor_u(a: #u8.0, b: #u8.0) -> #u8.0 {
    a ^ b
}
&^bit_not_u(a: #u8.0) -> #u8.0 {
    ~a
}
&^shift_lr_u(a: #u8.0, s: #u8.0) -> #u8.0 {
    a << s >> s
}
&^acc_u(a: #u8.0, b: #u8.0, s: #u8.0) -> #u8.0 {
    x = a;
    x &= b;
    x |= a;
    x ^= b;
    x <<= s;
    x >>= s;
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
