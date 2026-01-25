## Stdout
```
// Lowered Vexel module: tests/modules/NR-019/sprites_example/test.vx
sprites: #__Tuple2_s_s[2] = [("sprite1.png", "PNG_DATA_1"), ("sprite2.png", "PNG_DATA_2")];
&^main() -> #i32 {
    len = |sprites|;
    result = len == 2 ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
