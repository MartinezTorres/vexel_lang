## Stdout
```
// Lowered Vexel module: tests/expressions/EX-089/multi_assignment_syntax/test.vx
&^main() -> #i32 {
    a: #i32;
    b: #i32;
    {
        __tuple_tmp_0: #__Tuple2_i32_i32 = pair();
        a = __tuple_tmp_0.__0;
        b = __tuple_tmp_0.__1;
    };
    -> a + b;
}
```

## Stderr
```
```

## Exit Code
0
