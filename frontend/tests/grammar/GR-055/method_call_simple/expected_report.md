## Stdout
```
// Lowered Vexel module: tests/grammar/GR-055/method_call_simple/test.vx
&^main() -> #i32 {
    p = Point(0, 0);
    p = Point::move(1, 2);
    p.x + p.y
}
```

## Stderr
```
```

## Exit Code
0
