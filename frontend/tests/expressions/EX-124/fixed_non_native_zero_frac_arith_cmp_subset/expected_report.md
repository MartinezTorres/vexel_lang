## Stdout
```
// Lowered Vexel module: tests/expressions/EX-124/fixed_non_native_zero_frac_arith_cmp_subset/test.vx
&^arith72(a: #i72.0, b: #i72.0) -> #i72.0 {
    -a + b - a
}
&^cmp72(a: #i72.0, b: #i72.0) -> #b {
    a + b >= a - b
}
&^acc72(a: #u72.0, b: #u72.0) -> #u72.0 {
    x = a;
    x = b;
    x += a;
    x -= b;
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
