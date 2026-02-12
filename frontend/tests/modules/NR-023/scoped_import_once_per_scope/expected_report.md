## Stdout
```
// Lowered Vexel module: tests/modules/NR-023/scoped_import_once_per_scope/test.vx
&^main() -> #i32 {
    a: #i32 = 0;
    b: #i32 = 0;
    {
        ::counter;
        increment();
        a = getState()
    };
    {
        ::counter;
        b = getState()
    };
    result = a == 1 && b == 0 ? 0 : 1;
    -> result;
}
state: #i32;
&increment() -> #i32 {
    state = state + 1
}
&getState() -> #i32 {
    state
}
state: #i32;
&getState() -> #i32 {
    state
}
```

## Stderr
```
```

## Exit Code
0
