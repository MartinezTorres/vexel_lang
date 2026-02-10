## Stdout
```
// Lowered Vexel module: tests/declarations/DC-105/tuple_multi_values/test.vx
&^main() -> #i32 {
    a: #i32;
    b: #i32;
    c: #i32;
    {
        __tuple_tmp_0: #__Tuple3_i32_i32_i32 = triple();
        a = __tuple_tmp_0.__0;
        b = __tuple_tmp_0.__1;
        c = __tuple_tmp_0.__2;
    };
    a + b + c
}
```

## Stderr
```
```

## Exit Code
0
