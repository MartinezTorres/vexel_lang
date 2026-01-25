## Stdout
```
// Lowered Vexel module: tests/expressions/EX-094/assignment_no_aliasing/test.vx
#Point(x: #i32, y: #i32);
&^main() -> #i32 {
    p1 = Point(10, 20);
    p2 = p1;
    p2.x = 99;
    -> p1.x;
}
```

## Stderr
```
```

## Exit Code
0
