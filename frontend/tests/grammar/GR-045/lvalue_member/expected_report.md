## Stdout
```
// Lowered Vexel module: tests/grammar/GR-045/lvalue_member/test.vx
#Inner(v: #i32);
#Holder(field: #i32, inner: #Inner);
&^main() -> #i32 {
    obj = Holder(0, Inner(0));
    obj.field = 10;
    obj.inner.v = 20;
    obj.field + obj.inner.v
}
```

## Stderr
```
```

## Exit Code
0
