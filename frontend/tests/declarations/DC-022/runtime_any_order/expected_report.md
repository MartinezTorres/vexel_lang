## Stdout
```
// Lowered Vexel module: tests/declarations/DC-022/runtime_any_order/test.vx
&^main() -> #i32 {
    first() + second()
}
&first() -> #i32 {
    1
}
&second() -> #i32 {
    2
}
```

## Stderr
```
```

## Exit Code
0
