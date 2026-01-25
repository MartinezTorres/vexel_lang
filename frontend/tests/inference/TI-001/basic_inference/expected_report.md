## Stdout
```
// Lowered Vexel module: tests/inference/TI-001/basic_inference/test.vx
&identity(x) {
    x
}
&^main() -> #i32 {
    identity_G_i8(42)
}
&identity_G_i8(x: #i8) -> #i8 {
    x
}
```

## Stderr
```
```

## Exit Code
0
