## Stdout
```
// Lowered Vexel module: tests/grammar/GR-035/method_call/test.vx
#Point(x: #i32, y: #i32);
&(p)#Point::sum_with(other: #Point) -> #i32 {
    p.x + other.x + p.y + other.y
}
&^main() -> #i32 {
    obj = Point(1, 2);
    other = Point(3, 4);
    Point::sum_with(other)
}
```

## Stderr
```
```

## Exit Code
0
