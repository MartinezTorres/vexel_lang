## Stdout
```
// Lowered Vexel module: tests/declarations/DC-043/macro_like/test.vx
&unless($cond: #b, $body: #T0) {
    !cond ? 
        {
            body
        };
}
&^main() -> #i32 {
    x: #i32 = 5;
    unless(x > 10, x = 100);
    x
}
```

## Stderr
```
```

## Exit Code
0
