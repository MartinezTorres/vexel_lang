## Stdout
```
// Lowered Vexel module: tests/modules/NR-024/scoped_no_shared_state/test.vx
&^main() -> #i32 {
    {
        ::stateful;
        0;
        42
    };
    {
        ::stateful;
        0;
        val = get();
        result = val == 0 ? ( #i32 ) 0 : ( #i32 ) 1;
        -> result;
    }
}
value: #i32;
&get() -> #i32 {
    value
}
```

## Stderr
```
```

## Exit Code
0
