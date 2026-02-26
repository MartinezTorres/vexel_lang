## Stdout
```
// Lowered Vexel module: tests/expressions/EX-126/fixed_non_native_zero_frac_casts_cte/test.vx
&^main() -> #i32 {
    u72 = 300;
    s72 = -7;
    u80 = ( #u80.0 ) u72;
    s80 = ( #i80.0 ) s72;
    i32v = ( #i32 ) s72;
    u16v = ( #u16 ) u72;
    b0 = 0;
    b1 = ( #b ) u72;
    f = 42;
    back = ( #u72.0 ) f;
    bytes = ( #u8[9] ) u72;
    ok = u80 == 300 && s80 == -7 && i32v == -7 && u16v == 300 && !b0 && b1 && ( #i32 ) f == 42 && back == 42 && bytes[0] == 0 && bytes[7] == 1 && bytes[8] == 44;
    ok ? 0 : 1
}
```

## Stderr
```
```

## Exit Code
0
