## Stdout
```
// Lowered Vexel module: tests/grammar/GR-042/qname_simple/test.vx
&foo() -> #i32 {
    1
}
&^main() -> #i32 {
    x = foo();
    x
}
```

## Stderr
```
```

## Exit Code
0
