## Stdout
```
// Lowered Vexel module: tests/modules/NR-017/mutate_resource_reject/test.vx
content: #s = "immutable";
&^main() -> #i32 {
    content = "new value";
    0
}
```

## Stderr
```
```

## Exit Code
0
