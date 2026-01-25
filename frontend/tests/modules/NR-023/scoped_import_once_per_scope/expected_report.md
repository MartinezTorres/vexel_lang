## Stdout
```
// Lowered Vexel module: tests/modules/NR-023/scoped_import_once_per_scope/test.vx
&^main() -> #i32 {
    a = 0;
    b = 0;
    {
        ::counter;
        reset();
        increment();
        a = getState()
    };
    {
        ::counter;
        reset();
        b = getState()
    };
    result = a == 1 && b == 0 ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
mut state: #i32;
&reset() -> #i32 {
    state = 0
}
&increment() -> #i32 {
    state = state + 1
}
&getState() -> #i32 {
    state
}
mut state: #i32;
&reset() -> #i32 {
    state = 0
}
&increment() -> #i32 {
    state = state + 1
}
&getState() -> #i32 {
    state
}
```

## Stderr
```
```

## Exit Code
0
