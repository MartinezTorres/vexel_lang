## Stdout
```
// Lowered Vexel module: tests/expressions/EX-114/fixed_native_compound_add_sub/test.vx
&^acc_u(a: #u8.8, b: #u8.8) -> #u8.8 {
    x = a;
    x += b;
    x -= a;
    x
}
&^acc_s(a: #i10.6, b: #i10.6) -> #i10.6 {
    x = a;
    x += b;
    x -= a;
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
