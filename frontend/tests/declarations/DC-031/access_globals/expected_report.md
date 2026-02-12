## Stdout
```
// Lowered Vexel module: tests/declarations/DC-031/access_globals/test.vx
mutable: #i32;
&test() -> #i32 {
    mutable = 20;
    10 + mutable
}
&^main() -> #i32 {
    test()
}
```

## Stderr
```
```

## Exit Code
0
