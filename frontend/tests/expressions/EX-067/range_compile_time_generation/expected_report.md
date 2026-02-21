## Stdout
```
// Lowered Vexel module: tests/expressions/EX-067/range_compile_time_generation/test.vx
&^main() -> #i32 {
    START = 10;
    END = 15;
    r: #u8[5] = [10, 11, 12, 13, 14];
    -> ( #i32 ) r[2];
}
```

## Stderr
```
```

## Exit Code
0
