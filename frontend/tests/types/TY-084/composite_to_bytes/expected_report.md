## Stdout
```
// Lowered Vexel module: tests/types/TY-084/composite_to_bytes/test.vx
&^main() -> #i32 {
    p = Pair(10, 20);
    bytes = ( #u8[2] ) p;
    ( #i32 ) bytes[0]
}
```

## Stderr
```
```

## Exit Code
0
