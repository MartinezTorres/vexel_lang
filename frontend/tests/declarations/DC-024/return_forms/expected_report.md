## Stdout
```
// Lowered Vexel module: tests/declarations/DC-024/return_forms/test.vx
&implicit() -> #i32 {
    42
}
&explicit() -> #i32 {
    -> 100;
}
&^main() -> #i32 {
    implicit() + explicit()
}
```

## Stderr
```
```

## Exit Code
0
