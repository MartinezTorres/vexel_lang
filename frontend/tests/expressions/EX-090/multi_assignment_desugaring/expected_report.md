## Stdout
```
// Lowered Vexel module: tests/expressions/EX-090/multi_assignment_desugaring/test.vx
&get_pair() -> (#i32, #i32) {
    (100, 200)
}
&^main() -> #i32 {
    a = 0;
    b = 0;
    {
        mut __tuple_tmp_0: #__Tuple2_i32_i32 = get_pair();
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
