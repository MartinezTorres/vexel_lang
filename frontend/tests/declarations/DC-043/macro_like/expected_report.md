## Stdout
```
// Lowered Vexel module: tests/declarations/DC-043/macro_like/test.vx
&unless($cond: #T0, $body: #T1) {
    !cond ? 
        {
            body
        };
}
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
