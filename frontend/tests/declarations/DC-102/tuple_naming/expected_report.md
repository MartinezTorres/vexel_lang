## Stdout
```
// Lowered Vexel module: tests/declarations/DC-102/tuple_naming/test.vx
&^main() -> #i32 {
    a: #i32;
    b: #i32;
    {
        __tuple_tmp_0: #__Tuple2_i32_i32 = values();
        a = __tuple_tmp_0.__0;
        b = __tuple_tmp_0.__1;
    };
    a + b
}
```

## Stderr
```
```

## Exit Code
0
