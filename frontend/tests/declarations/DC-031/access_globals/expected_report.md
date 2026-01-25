## Stdout
```
// Lowered Vexel module: tests/declarations/DC-031/access_globals/test.vx
IMMUTABLE: #i8 = 10;
mut mutable: #i32;
&test() -> #i32 {
    mutable = 20;
    IMMUTABLE + mutable
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
