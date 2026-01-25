## Stdout
```
// Lowered Vexel module: tests/modules/NR-040/forward_ref_function/test.vx
&caller() -> #i32 {
    callee()
}
&callee() -> #i32 {
    42
}
&^main() -> #i32 {
    caller()
}
```

## Stderr
```
```

## Exit Code
0
