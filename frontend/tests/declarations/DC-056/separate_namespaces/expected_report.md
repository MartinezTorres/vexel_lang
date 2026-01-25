## Stdout
```
// Lowered Vexel module: tests/declarations/DC-056/separate_namespaces/test.vx
#Stack(top: #i32);
&(s)#Stack::push() -> #i32 {
    s.top = s.top + 1
}
&push() -> #i32 {
    42
}
&^main() -> #i32 {
    push()
}
```

## Stderr
```
```

## Exit Code
0
