## Stdout
```
// Lowered Vexel module: tests/declarations/DC-076/iterator_names/test.vx
#List(items: #i32[5]);
&(l)#List::@($loop: #T0) -> #T0 {
    _ = l.items[0];
    loop
}
&^main() -> #i32 {
    lst = List([1, 2, 3, 4, 5]);
    List::@({
        -> 0;
    });
    0
}
```

## Stderr
```
```

## Exit Code
0
