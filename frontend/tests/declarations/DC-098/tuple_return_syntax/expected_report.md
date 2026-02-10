## Stdout
```
// Lowered Vexel module: tests/declarations/DC-098/tuple_return_syntax/test.vx
&^main() -> #i32 {
    quotient: #i32;
    remainder: #i32;
    {
        __tuple_tmp_0: #__Tuple2_i32_i32 = divide(17, 5);
        quotient = __tuple_tmp_0.__0;
        remainder = __tuple_tmp_0.__1;
    };
    quotient + remainder
}
```

## Stderr
```
```

## Exit Code
0
