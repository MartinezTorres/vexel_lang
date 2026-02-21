## Stdout
```
// Lowered Vexel module: tests/declarations/DC-074/iteration_syntax/test.vx
#Range(start: #i32, end: #i32);
&(r)#Range::@($loop: #T0) -> #T0 {
    _ = r.start;
    loop
}
&^main() -> #i32 {
    first = 0;
    rng = Range(1, 5);
    Range::@({
        first = _
    });
    first
}
```

## Stderr
```
```

## Exit Code
0
