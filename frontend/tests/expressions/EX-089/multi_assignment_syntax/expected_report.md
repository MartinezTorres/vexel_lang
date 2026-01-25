## Stdout
```
// Lowered Vexel module: tests/expressions/EX-089/multi_assignment_syntax/test.vx
&pair() -> (#i32, #i32) {
    (100, 200)
}
&^main() -> #i32 {
    mut a: #i32;
    mut b: #i32;
    {
        mut __tuple_tmp_0: #__Tuple2_i32_i32 = pair();
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
