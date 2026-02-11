## Stdout
```
// Lowered Vexel module: tests/inference/TI-014/nested_capture/test.vx
&^main() -> #i32 {
    outer: #i32 = 10;
    &inner() -> #i32 {
        outer
    }
    inner()
}
```

## Stderr
```
```

## Exit Code
0
