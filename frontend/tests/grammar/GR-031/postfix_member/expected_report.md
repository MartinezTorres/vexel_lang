## Stdout
```
// Lowered Vexel module: tests/grammar/GR-031/postfix_member/test.vx
#Inner(v: #i32);
#Outer(inner: #Inner);
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
