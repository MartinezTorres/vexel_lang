## Stdout
```
// Lowered Vexel module: tests/declarations/DC-101/multi_assign_desugar/test.vx
&split(val: #i32) -> (#i32, #i32) {
    (val / 2, val - val / 2)
}
&^main() -> #i32 {
    mut low: #i32;
    mut high: #i32;
    {
        mut __tuple_tmp_0: #__Tuple2_i32_i32 = split(10);
        low = __tuple_tmp_0.__0;
        high = __tuple_tmp_0.__1;
    };
    low + high
}
```

## Stderr
```
```

## Exit Code
0
