## Stdout
```
// Lowered Vexel module: tests/modules/NR-038/const_parse_order/test.vx
FIRST: #i32 = 1;
SECOND: #i32 = FIRST + 1;
THIRD: #i32 = SECOND + 1;
&^main() -> #i32 {
    result = THIRD == 3 ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
