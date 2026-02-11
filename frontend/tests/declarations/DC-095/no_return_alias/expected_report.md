## Stdout
```
// Lowered Vexel module: tests/declarations/DC-095/no_return_alias/test.vx
&(elem)process() -> #i32 {
    elem * 2
}
&^main() -> #i32 {
    x: #i32 = 21;
    process()
}
```

## Stderr
```
```

## Exit Code
0
