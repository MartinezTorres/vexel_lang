## Stdout
```
// Lowered Vexel module: tests/declarations/DC-038/pass_by_value/test.vx
&modify(x: #i32) -> #i32 {
    temp = x;
    temp = temp + 1;
    temp
}
&^main() -> #i32 {
    a = 10;
    b = modify(a);
    a
}
```

## Stderr
```
```

## Exit Code
0
