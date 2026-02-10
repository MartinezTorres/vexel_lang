## Stdout
```
// Lowered Vexel module: tests/grammar/GR-035/method_call/test.vx
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
