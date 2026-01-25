## Stdout
```
// Lowered Vexel module: tests/declarations/DC-098/tuple_return_syntax/test.vx
&divide(a: #i32, b: #i32) -> (#i32, #i32) {
    q = a / b;
    r = a - q * b;
    (q, r)
}
&^main() -> #i32 {
    mut quotient: #i32;
    mut remainder: #i32;
    {
        mut __tuple_tmp_0: #__Tuple2_i32_i32 = divide(17, 5);
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
