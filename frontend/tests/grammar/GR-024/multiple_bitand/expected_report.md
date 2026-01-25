## Stdout
```
// Lowered Vexel module: tests/grammar/GR-024/multiple_bitand/test.vx
&^main() -> #i32 {
    result = ( #u8 ) 15 & ( #u8 ) 7 & ( #u8 ) 3 & ( #u8 ) 1;
    result != ( #u8 ) 0 ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
