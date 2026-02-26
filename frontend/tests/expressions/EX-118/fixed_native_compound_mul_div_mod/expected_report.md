## Stdout
```
// Lowered Vexel module: tests/expressions/EX-118/fixed_native_compound_mul_div_mod/test.vx
&^acc_u(a: #u8.8, b: #u8.8, c: #u8.8) -> #u8.8 {
    x = a;
    x *= b;
    x /= c;
    x %= b;
    x
}
&^acc_s(a: #i8.8, b: #i8.8, c: #i8.8) -> #i8.8 {
    x = a;
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
