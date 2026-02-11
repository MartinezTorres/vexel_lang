## Stdout
```
// Lowered Vexel module: tests/declarations/DC-075/custom_iteration/test.vx
#Container(data: #i32[3]);
&(c)#Container::@($loop: #T0) -> #T0 {
    _ = c.data[0];
    loop
}
&^main() -> #i32 {
    cnt = Container([1, 2, 3]);
    sum: #i32 = 0;
    Container::@({
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
