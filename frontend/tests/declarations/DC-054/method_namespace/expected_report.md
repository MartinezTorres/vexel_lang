## Stdout
```
// Lowered Vexel module: tests/declarations/DC-054/method_namespace/test.vx
#Widget(id: #i32);
&(w)#Widget::show() -> #i32 {
    w.id
}
&^main() -> #i32 {
    widget = Widget(123);
    Widget::show()
}
```

## Stderr
```
```

## Exit Code
0
