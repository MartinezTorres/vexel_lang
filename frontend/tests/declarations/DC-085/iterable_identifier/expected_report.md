## Stdout
```
// Lowered Vexel module: tests/declarations/DC-085/iterable_identifier/test.vx
#Iterable(count: #i32);
&(it)#Iterable::@($loop: #T0) -> #T0 {
    _ = it.count;
    loop
}
&^main() -> #i32 {
    obj = Iterable(3);
    sum: #i32 = 0;
    Iterable::@({
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
