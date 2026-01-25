## Stdout
```
// Lowered Vexel module: tests/declarations/DC-026/return_operator/test.vx
&test() -> #i32 {
    -> 42;
}
&empty() {
    ->;
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
