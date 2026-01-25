## Stdout
```
// Lowered Vexel module: tests/declarations/DC-096/no_capture_alias/test.vx
&(data)operate() -> #i32 {
    data + 1
}
&^main() -> #i32 {
    value = 50;
    operate()
}
```

## Stderr
```
```

## Exit Code
0
