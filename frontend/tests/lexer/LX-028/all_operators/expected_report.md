## Stdout
```
// Lowered Vexel module: tests/lexer/LX-028/all_operators/test.vx
&^main() -> #i32 {
    a = ( #u32 ) 1 + ( #u32 ) 2 - ( #u32 ) 3 * ( #u32 ) 4 / ( #u32 ) 5 % ( #u32 ) 6;
    b = a & ( #u32 ) 7 | ( #u32 ) 8 ^ ( #u32 ) 9;
    c = ~a;
    d = b << ( #u32 ) 1 >> ( #u32 ) 1;
    e = a == b;
    f = a != b;
    g = a < b;
    h = a <= b;
    i = a > b;
    j = a >= b;
    k = e && f || g;
    x = 0;
    x = 42;
    0
}
```

## Stderr
```
```

## Exit Code
0
