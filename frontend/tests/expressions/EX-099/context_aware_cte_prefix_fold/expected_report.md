## Stdout
```
// Lowered Vexel module: tests/expressions/EX-099/context_aware_cte_prefix_fold/test.vx
&^f(flag: #b) -> #i32 {
    a: #i32 = 0;
    a = a + 1;
    flag ? 
        {
            a = a + 1
        };
    a
}
```

## Stderr
```
```

## Exit Code
0
