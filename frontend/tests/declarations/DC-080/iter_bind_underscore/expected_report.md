## Stdout
```
// Lowered Vexel module: tests/declarations/DC-080/iter_bind_underscore/test.vx
#Elements(arr: #i32[3]);
&(e)#Elements::@($loop: #T0) -> #T0 {
    _ = e.arr[0];
    loop
}
&^main() -> #i32 {
    els = Elements([10, 20, 30]);
    sum: #i32 = 0;
    Elements::@({
        sum = sum + _
    });
    sum
}
```

## Stderr
```
```

## Exit Code
0
