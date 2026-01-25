## Stdout
```
// Lowered Vexel module: tests/declarations/DC-066/operator_no_expr_param/test.vx
#Type(v: #i32);
&(t)#Type::+(other: #Type) -> #Type {
    Type(t.v + other.v)
}
&^main() -> #i32 {
    0
}
```

## Stderr
```
```

## Exit Code
0
