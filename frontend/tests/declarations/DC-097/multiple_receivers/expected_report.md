## Stdout
```
// Lowered Vexel module: tests/declarations/DC-097/multiple_receivers/test.vx
&(a, b, c)sum_three() -> #i32 {
    a + b + c
}
&^main() -> #i32 {
    x = 1;
    y = 2;
    z = 3;
    sum_three()
}
```

## Stderr
```
```

## Exit Code
0
