## Stdout
```
// Lowered Vexel module: tests/declarations/DC-094/no_escape_global/test.vx
&(item)use() -> #i32 {
    item
}
&^main() -> #i32 {
    val = 99;
    use()
}
```

## Stderr
```
```

## Exit Code
0
