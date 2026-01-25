## Stdout
```
// Lowered Vexel module: tests/declarations/DC-039/valid_identifiers/test.vx
&valid_name() -> #i32 {
    1
}
&another_valid_name() -> #i32 {
    2
}
&^main() -> #i32 {
    valid_name() + another_valid_name()
}
```

## Stderr
```
```

## Exit Code
0
