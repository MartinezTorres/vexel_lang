## Stdout
```
// Lowered Vexel module: tests/declarations/DC-105/tuple_multi_values/test.vx
&triple() -> (#i32, #i32, #i32) {
    (1, 2, 3)
}
&^main() -> #i32 {
    mut a: #i32;
    mut b: #i32;
    mut c: #i32;
    {
        mut __tuple_tmp_0: #__Tuple3_i32_i32_i32 = triple();
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
