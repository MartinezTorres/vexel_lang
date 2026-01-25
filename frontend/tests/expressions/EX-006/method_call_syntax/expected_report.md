## Stdout
```
// Lowered Vexel module: tests/expressions/EX-006/method_call_syntax/test.vx
#Point(x: #i32, y: #i32);
&(self)#Point::distance_from_origin() -> #i32 {
    -> self.x * self.x + self.y * self.y;
}
&^main() -> #i32 {
    p = Point(3, 4);
    d = Point::distance_from_origin();
    -> d;
}
```

## Stderr
```
```

## Exit Code
0
