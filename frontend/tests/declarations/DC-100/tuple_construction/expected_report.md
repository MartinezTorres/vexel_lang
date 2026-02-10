## Stdout
```
// Lowered Vexel module: tests/declarations/DC-100/tuple_construction/test.vx
&^main() -> #i32 {
    x: #i32;
    y: #i32;
    {
        __tuple_tmp_0: #__Tuple2_i32_i32 = get_coords();
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
