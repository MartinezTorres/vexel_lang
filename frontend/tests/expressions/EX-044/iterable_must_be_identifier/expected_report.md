## Stdout
```
// Lowered Vexel module: tests/expressions/EX-044/iterable_must_be_identifier/test.vx
&print(arg0: #i32) {
}
&^main() -> #i32 {
    arr = [1, 2, 3];
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
