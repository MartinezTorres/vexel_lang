## Stdout
```
// Lowered Vexel module: tests/declarations/DC-103/tuple_internal_names/test.vx
&data() -> (#i32, #i32) {
    (5, 10)
}
&^main() -> #i32 {
    x: #i32;
    y: #i32;
    {
        __tuple_tmp_0: #__Tuple2_i32_i32 = data();
        x = __tuple_tmp_0.__0;
        y = __tuple_tmp_0.__1;
    };
    x + y
}
```

## Stderr
```
```

## Exit Code
0
