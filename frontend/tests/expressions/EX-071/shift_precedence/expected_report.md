## Stdout
```
// Lowered Vexel module: tests/expressions/EX-071/shift_precedence/test.vx
&^main() -> #i32 {
    x = ( #u32 ) 4 << ( #u32 ) 1;
    y = ( #u32 ) 16 >> ( #u32 ) 2;
    -> x + y;
}
```

## Stderr
```
```

## Exit Code
0
