## Stdout
```
// Lowered Vexel module: tests/expressions/EX-130/fixed_bitwise_shift_signed_fractional/test.vx
&!sx() -> #i40.8;
&!sy() -> #i40.8;
&^main() -> #i32 {
    x = sx();
    y = sy();
    a = x & y;
    b = x | y;
    c = x ^ y;
    d = x >> y;
    e = x << y;
    f = ~x;
    z = ( #u40.8 ) x;
    u = z & z;
    v = z >> z;
    k = x;
    k &= y;
    k |= y;
    k ^= y;
    k >>= y;
    k <<= y;
    m = z;
    m &= z;
    m |= z;
    m ^= z;
    m >>= z;
    m <<= z;
    0
}
```

## Stderr
```
```

## Exit Code
0
