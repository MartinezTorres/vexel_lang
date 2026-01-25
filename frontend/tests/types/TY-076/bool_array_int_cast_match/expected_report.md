## Stdout
```
// Lowered Vexel module: tests/types/TY-076/bool_array_int_cast_match/test.vx
&^main() -> #i32 {
    b = [1, 0, 1, 0, 1, 0, 1, 0];
    u = ( #u8 ) b;
    ( #i32 ) u
}
```

## Stderr
```
```

## Exit Code
0
