## Stdout
```
// Lowered Vexel module: tests/declarations/DC-101/multi_assign_desugar/test.vx
&^main() -> #i32 {
    low: #i32;
    high: #i32;
    {
        __tuple_tmp_0: #__Tuple2_i32_i32 = split(10);
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
