## Stdout
```
// Lowered Vexel module: tests/declarations/DC-043/macro_like/test.vx
&^main() -> #i32 {
    x = 5;
    unless(x > 10, x = 100);
    x
}
```

## Stderr
```
```

## Exit Code
0
