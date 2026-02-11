## Stdout
```
// Lowered Vexel module: tests/types/TY-041/array_lexicographic_compare/test.vx
&^main() -> #i32 {
    a: #i32[3] = [1, 2, 3];
    b: #i32[3] = [1, 2, 4];
    a < b ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
