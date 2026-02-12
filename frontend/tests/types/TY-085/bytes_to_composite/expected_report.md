## Stdout
```
// Lowered Vexel module: tests/types/TY-085/bytes_to_composite/test.vx
#Pair(a: #u8, b: #u8);
&^main() -> #i32 {
    bytes: #u8[2] = [10, 20];
    p = ( #Pair ) bytes;
    ( #i32 ) p.a
}
```

## Stderr
```
```

## Exit Code
0
