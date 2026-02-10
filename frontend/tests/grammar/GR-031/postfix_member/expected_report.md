## Stdout
```
// Lowered Vexel module: tests/grammar/GR-031/postfix_member/test.vx
&^main() -> #i32 {
    o = Outer(Inner(5));
    value = o.inner.v;
    value
}
```

## Stderr
```
```

## Exit Code
0
