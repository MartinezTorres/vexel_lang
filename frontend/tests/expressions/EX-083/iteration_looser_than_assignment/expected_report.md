## Stdout
```
// Lowered Vexel module: tests/expressions/EX-083/iteration_looser_than_assignment/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr = [1, 2, 3];
    x = 0;
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
