## Stdout
```
// Lowered Vexel module: tests/expressions/EX-027/iterable_evaluates_once/test.vx
&!print(arg0: #i32);
mut counter: #i32;
&get_array() -> #i32[3] {
    counter = counter + 1;
    print(counter);
    -> [1, 2, 3];
}
&^main() -> #i32 {
    counter = 0;
    arr = get_array();
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
