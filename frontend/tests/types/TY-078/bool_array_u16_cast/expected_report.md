## Stdout
```
// Lowered Vexel module: tests/types/TY-078/bool_array_u16_cast/test.vx
&^main() -> #i32 {
    b = [1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0];
    u = ( #u16 ) b;
    ( #i32 ) u
}
```

## Stderr
```
```

## Exit Code
0
