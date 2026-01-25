## Stdout
```
// Lowered Vexel module: tests/grammar/GR-055/method_call_simple/test.vx
#Point(x: #i32, y: #i32);
&(p)#Point::move(dx: #i32, dy: #i32) -> #Point {
    Point(p.x + dx, p.y + dy)
}
&^main() -> #i32 {
    mut p: #Point = Point(0, 0);
    p = Point::move(1, 2);
    p.x + p.y
}
```

## Stderr
```
```

## Exit Code
0
