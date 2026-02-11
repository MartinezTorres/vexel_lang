## Stdout
```
// Lowered Vexel module: tests/grammar/GR-022/multiple_bitor/test.vx
&^main() -> #i32 {
    result = ( #u8 ) 1 | ( #u8 ) 2 | ( #u8 ) 4 | ( #u8 ) 8;
    result != ( #u8 ) 0 ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
