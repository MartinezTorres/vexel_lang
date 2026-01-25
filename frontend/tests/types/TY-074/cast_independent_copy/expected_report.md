## Stdout
```
// Lowered Vexel module: tests/types/TY-074/cast_independent_copy/test.vx
#Point(x: #i32, y: #i32);
&^main() -> #i32 {
    p1 = Point(10, 20);
    p2 = p1;
    p2.x
}
```

## Stderr
```
```

## Exit Code
0
