## Stdout
```
// Lowered Vexel module: tests/grammar/GR-023/multiple_xor/test.vx
&^main() -> #i32 {
    result = ( #u8 ) 1 ^ ( #u8 ) 2 ^ ( #u8 ) 3 ^ ( #u8 ) 4;
    result != ( #u8 ) 0 ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
