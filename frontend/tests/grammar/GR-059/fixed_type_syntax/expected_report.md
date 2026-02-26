## Stdout
```
// Lowered Vexel module: tests/grammar/GR-059/fixed_type_syntax/test.vx
&^wrap_u(v: #u8.8) -> #u8.8 {
    v
}
&^wrap_s(v: #i10.6) -> #i10.6 {
    v
}
&^wrap_coarse(v: #i4.-2) -> #i4.-2 {
    v
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
