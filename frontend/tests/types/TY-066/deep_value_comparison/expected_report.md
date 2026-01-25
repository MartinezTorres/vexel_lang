## Stdout
```
// Lowered Vexel module: tests/types/TY-066/deep_value_comparison/test.vx
#Point(x: #i32, y: #i32);
&^main() -> #i32 {
    p1 = Point(10, 20);
    p2 = Point(10, 20);
    p1 == p2 ? 1 : 0
}
```

## Stderr
```
```

## Exit Code
0
