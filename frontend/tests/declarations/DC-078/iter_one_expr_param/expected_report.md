## Stdout
```
// Lowered Vexel module: tests/declarations/DC-078/iter_one_expr_param/test.vx
#Series(max: #i32);
&(s)#Series::@($loop: #T0) -> #T0 {
    _ = s.max;
    loop
}
&^main() -> #i32 {
    series = Series(4);
    Series::@({
        -> 0;
    });
    0
}
```

## Stderr
```
```

## Exit Code
0
