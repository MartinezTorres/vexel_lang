## Stdout
```
// Lowered Vexel module: tests/declarations/DC-033/no_outlive/test.vx
&test() -> #i32 {
    x = 10;
    &inner() -> #i32 {
        x
    }
    inner()
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
