## Stdout
```
// Lowered Vexel module: tests/types/TY-062/array_lex_compare_same_length/test.vx
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
