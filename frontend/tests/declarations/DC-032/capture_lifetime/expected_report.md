## Stdout
```
// Lowered Vexel module: tests/declarations/DC-032/capture_lifetime/test.vx
&test() -> #i32 {
    x: #i32 = 5;
    &use_x() -> #i32 {
        x
    }
    use_x()
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
