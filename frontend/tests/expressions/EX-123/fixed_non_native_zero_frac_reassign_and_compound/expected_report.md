## Stdout
```
// Lowered Vexel module: tests/expressions/EX-123/fixed_non_native_zero_frac_reassign_and_compound/test.vx
&^copy72(a: #u72.0, b: #u72.0, s: #u72.0) -> #u72.0 {
    x = a;
    x = b;
    x &= a;
    x |= b;
    x ^= a;
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
