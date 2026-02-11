## Stdout
```
// Lowered Vexel module: tests/declarations/DC-079/iter_inline/test.vx
#Counter(limit: #i32);
&(c)#Counter::@($loop: #T0) -> #T0 {
    _ = c.limit;
    loop
}
&^main() -> #i32 {
    ctr = Counter(3);
    total: #i32 = 0;
    Counter::@({
        total = total + _
    });
    total
}
```

## Stderr
```
```

## Exit Code
0
