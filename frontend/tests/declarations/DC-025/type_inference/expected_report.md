## Stdout
```
// Lowered Vexel module: tests/declarations/DC-025/type_inference/test.vx
&inferred(x: #i32) -> #i32 {
    x * 2
}
&^main() -> #i32 {
    inferred(21)
}
```

## Stderr
```
```

## Exit Code
0
