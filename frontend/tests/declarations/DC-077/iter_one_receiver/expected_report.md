## Stdout
```
// Lowered Vexel module: tests/declarations/DC-077/iter_one_receiver/test.vx
#Sequence(count: #i32);
&(s)#Sequence::@($loop: #T0) -> #T0 {
    _ = s.count;
    loop
}
&^main() -> #i32 {
    seq = Sequence(3);
    Sequence::@({
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
