## Stdout
```
// Lowered Vexel module: tests/types/TY-083/bool_array_msb_bitN/test.vx
&^main() -> #i32 {
    b = [0, 0, 0, 0, 0, 0, 0, 1];
    u = ( #u8 ) b;
    ( #i32 ) u
}
```

## Stderr
```
```

## Exit Code
0
