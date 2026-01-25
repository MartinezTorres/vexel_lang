## Stdout
```
// Lowered Vexel module: tests/types/TY-058/function_generic_instantiation/test.vx
&identity(x) -> #i32 {
    x
}
&^main() -> #i32 {
    identity_G_i8(42)
}
&identity_G_i8(x: #i8) -> #i32 {
    x
}
```

## Stderr
```
```

## Exit Code
0
