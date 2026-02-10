## Stdout
```
// Lowered Vexel module: tests/modules/NR-024/scoped_no_shared_state/test.vx
&^main() -> #i32 {
    {
        ::stateful;
        reset();
        set(42)
    };
    {
        ::stateful;
        reset();
        val = get();
        result = val == 0 ? ( #i32 ) 0 : ( #i32 ) 1;
        -> result;
    }
}
value: #i32;
&reset() -> #i32 {
    value = 0
}
&set(v: #i32) -> #i32 {
    value = v
}
value: #i32;
&reset() -> #i32 {
    value = 0
}
&get() -> #i32 {
    value
}
```

## Stderr
```
```

## Exit Code
0
