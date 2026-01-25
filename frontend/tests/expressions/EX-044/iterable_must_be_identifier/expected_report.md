## Stdout
```
// Lowered Vexel module: tests/expressions/EX-044/iterable_must_be_identifier/test.vx
&print(arg0: #i32) {
}
&get_array() -> #i32[3] {
    -> [1, 2, 3];
}
&^main() -> #i32 {
    arr = get_array();
    arr@print(_);
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
