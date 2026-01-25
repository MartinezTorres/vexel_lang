## Stdout
```
// Lowered Vexel module: tests/declarations/DC-011/mutable_modification/test.vx
mut state: #i32;
&^main() -> #i32 {
    state = 10;
    state = 20;
    state
}
```

## Stderr
```
```

## Exit Code
0
