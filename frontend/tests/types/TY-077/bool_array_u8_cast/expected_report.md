## Stdout
```
// Lowered Vexel module: tests/types/TY-077/bool_array_u8_cast/test.vx
&^main() -> #i32 {
    b = [1, 1, 1, 1, 0, 0, 0, 0];
    u = ( #u8 ) b;
    ( #i32 ) u
}
```

## Stderr
```
```

## Exit Code
0
