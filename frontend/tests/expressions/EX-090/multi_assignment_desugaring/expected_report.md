## Stdout
```
// Lowered Vexel module: tests/expressions/EX-090/multi_assignment_desugaring/test.vx
&^main() -> #i32 {
    a: #i32 = 0;
    b: #i32 = 0;
    {
        __tuple_tmp_0: #__Tuple2_i32_i32 = get_pair();
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
