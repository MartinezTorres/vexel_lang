## Stdout
```
// Lowered Vexel module: tests/types/TY-104/move_optimization/test.vx
&^create() -> #i32[10] {
    [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
}
&^main() -> #i32 {
    arr = create();
    arr[0]
}
```

## Stderr
```
```

## Exit Code
0
