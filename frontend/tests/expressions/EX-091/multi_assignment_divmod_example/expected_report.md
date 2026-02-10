## Stdout
```
// Lowered Vexel module: tests/expressions/EX-091/multi_assignment_divmod_example/test.vx
&divmod(a: #u32, b: #u32) -> (#u32, #u32) {
    (a / b, a % b)
}
&^main() -> #i32 {
    q: #u32;
    r: #u32;
    {
        __tuple_tmp_0: #__Tuple2_u32_u32 = divmod(10, 3);
        q = __tuple_tmp_0.__0;
        r = __tuple_tmp_0.__1;
    };
    -> ( #i32 ) (q + r);
}
```

## Stderr
```
```

## Exit Code
0
