## Stdout
```
// Lowered Vexel module: tests/declarations/DC-049/method_syntax/test.vx
#Point(x: #i32, y: #i32);
&(p)#Point::distance() -> #i32 {
    p.x * p.x + p.y * p.y
}
&^main() -> #i32 {
    pt = Point(3, 4);
    Point::distance()
}
```

## Stderr
```
```

## Exit Code
0
