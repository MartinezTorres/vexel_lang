## Stdout
```
// Lowered Vexel module: tests/types/TY-086/composite_bytes_portable/test.vx
#RGB(r: #u8, g: #u8, b: #u8);
&^main() -> #i32 {
    c = RGB(255, 128, 64);
    bytes = ( #u8[3] ) c;
    0
}
```

## Stderr
```
```

## Exit Code
0
