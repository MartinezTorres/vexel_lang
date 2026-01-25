## Stdout
```
// Lowered Vexel module: tests/declarations/DC-018/anywhere_definition/test.vx
&early() -> #i32 {
    late()
}
&late() -> #i32 {
    42
}
&^main() -> #i32 {
    early()
}
```

## Stderr
```
```

## Exit Code
0
