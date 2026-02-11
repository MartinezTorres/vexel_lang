## Stdout
```
// Lowered Vexel module: tests/expressions/EX-083/iteration_looser_than_assignment/test.vx
&^main() -> #i32 {
    arr: #i32[3] = [1, 2, 3];
    x: #i32 = 0;
    arr@{
            x = _;
        };
    -> x;
}
```

## Stderr
```
```

## Exit Code
0
