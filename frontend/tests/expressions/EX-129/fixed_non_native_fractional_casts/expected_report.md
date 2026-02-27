## Stdout
```
// Lowered Vexel module: tests/expressions/EX-129/fixed_non_native_fractional_casts/test.vx
&!ua() -> #u40.32;
&!sa() -> #i40.32;
&^main() -> #i32 {
    u = ua();
    s = sa();
    u16f = ( #u16.8 ) u;
    u40f = ( #u40.32 ) u16f;
    i24 = ( #i24 ) s;
    i40f = ( #i40.32 ) i24;
    f = ( #f64 ) s;
    s2 = ( #i40.32 ) f;
    b = ( #b ) s2;
    u40 = ( #u40 ) s2;
    b ? ( #i32 ) u40 : ( #i32 ) i40f
}
```

## Stderr
```
```

## Exit Code
0
