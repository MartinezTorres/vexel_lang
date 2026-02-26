## Stdout
```
// Lowered Vexel module: tests/expressions/EX-115/fixed_native_cast_subset/test.vx
&^up_u(x: #u16.0) -> #u8.8 {
    ( #u8.8 ) x
}
&^down_u(x: #u8.8) -> #u16.0 {
    ( #u16.0 ) x
}
&^step_u(x: #u8.0) -> #u10.-2 {
    ( #u10.-2 ) x
}
&^unstep_u(x: #u10.-2) -> #u8.0 {
    ( #u8.0 ) x
}
&^nz_u(x: #u8.8) -> #b {
    ( #b ) x
}
&^retag_s(x: #i8.8) -> #i10.6 {
    ( #i10.6 ) x
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
