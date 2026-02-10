## Stdout
```
// Lowered Vexel module: tests/grammar/GR-036/constructor_args/test.vx
&^main() -> #i32 {
    p = Point(10, 20);
    rect = Rectangle(0, 0, 100, 100);
    p.x + rect.w
}
```

## Stderr
```
```

## Exit Code
0
