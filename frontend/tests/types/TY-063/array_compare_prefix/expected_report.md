## Stdout
```
// Lowered Vexel module: tests/types/TY-063/array_compare_prefix/test.vx
&^main() -> #i32 {
    a: #i32[2] = [1, 2];
    b: #i32[3] = [1, 2, 3];
    a < b ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
