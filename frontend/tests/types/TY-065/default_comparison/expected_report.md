## Stdout
```
// Lowered Vexel module: tests/types/TY-065/default_comparison/test.vx
#Value(x: #i32);
&^main() -> #i32 {
    v1 = Value(10);
    v2 = Value(20);
    v1 < v2 ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
