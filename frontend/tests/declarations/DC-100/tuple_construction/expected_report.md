## Stdout
```
// Lowered Vexel module: tests/declarations/DC-100/tuple_construction/test.vx
&get_coords() -> (#i32, #i32) {
    (42, 84)
}
&^main() -> #i32 {
    mut x: #i32;
    mut y: #i32;
    {
        mut __tuple_tmp_0: #__Tuple2_i32_i32 = get_coords();
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
