## Stdout
```
// Lowered Vexel module: tests/expressions/EX-100/compound_assignments_roundtrip/test.vx
&!putchar(c: #u8);
&^main() -> #i32 {
    putchar(0);
    a = 10;
    u = 3;
    b = 0;
    a += 2;
    a -= 1;
    a *= 4;
    a /= 2;
    u %= 3;
    u |= 4;
    u &= 6;
    u ^= 2;
    u <<= 1;
    u >>= 1;
    b ||= 1;
    b &&= 1;
    a + ( #i32 ) u + ( #i32 ) b
}
```

## Stderr
```
```

## Exit Code
0
