## Stdout
```
// Lowered Vexel module: tests/modules/NR-011/empty_directory/test.vx
items: #__Tuple2_#s_#s[0] = [];
&^main() -> #i32 {
    len = |items|;
    result = len == 0 ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
