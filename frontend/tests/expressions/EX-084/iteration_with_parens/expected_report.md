## Stdout
```
// Lowered Vexel module: tests/expressions/EX-084/iteration_with_parens/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr = [1, 2, 3];
    arr@print(_ + 1);
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
