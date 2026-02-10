## Stdout
```
// Lowered Vexel module: tests/grammar/GR-039/chained_member/test.vx
&^main() -> #i32 {
    o = Outer(Inner(7));
    x = o.inner.v;
    x
}
```

## Stderr
```
```

## Exit Code
0
