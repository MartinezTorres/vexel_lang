## Stdout
```
// Lowered Vexel module: tests/declarations/DC-106/tuple_field_names/test.vx
&nums() -> (#i32, #i32) {
    (7, 14)
}
&^main() -> #i32 {
    result = nums();
    result.__0 + result.__1
}
```

## Stderr
```
```

## Exit Code
0
