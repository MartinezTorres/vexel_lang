## Stdout
```
// Lowered Vexel module: tests/modules/NR-023/scoped_import_once_per_scope/test.vx
&^main() -> #i32 {
    a = 0;
    b = 0;
    result = a == 1 && b == 0 ? 0 : 1;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
