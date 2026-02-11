## Stdout
```
// Lowered Vexel module: tests/expressions/EX-048/repeat_vs_iterable_form/test.vx
&print(arg0: #i32) {
}
&^main() -> #i32 {
    i: #i32 = 0;
    i < 3@{
            print(i);
            i = i + 1
        };
    arr: #i32[3] = [1, 2, 3];
    arr@{
            print(_);
        };
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
