## Stdout
```
// Lowered Vexel module: tests/lexer/LX-029/multichar_operators/test.vx
&^main() -> #i32 {
    a = ( #u32 ) 1 << ( #u32 ) 2;
    b = ( #u32 ) 8 >> ( #u32 ) 1;
    c = a == b;
    d = a != b;
    e = a <= b;
    f = a >= b;
    g = c && d;
    h = e || f;
    0
}
```

## Stderr
```
```

## Exit Code
0
