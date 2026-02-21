## Stdout
```
// Lowered Vexel module: tests/declarations/DC-081/iter_pattern/test.vx
#Collection(data: #i32[4]);
&(c)#Collection::@($loop: #T0) -> #T0 {
    _ = c.data[0];
    loop
}
&^main() -> #i32 {
    coll = Collection([1, 2, 3, 4]);
    product = 1;
    Collection::@({
        product = product * _
    });
    product
}
```

## Stderr
```
```

## Exit Code
0
