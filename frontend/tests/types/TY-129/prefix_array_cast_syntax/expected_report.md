## Stdout
```
// Lowered Vexel module: tests/types/TY-129/prefix_array_cast_syntax/test.vx
&^main() -> #i32 {
    bytes = [1, 2, 3, 4];
    packed = ( #u8[4] ) bytes;
    ( #i32 ) packed
}
```

## Stderr
```
```

## Exit Code
0
