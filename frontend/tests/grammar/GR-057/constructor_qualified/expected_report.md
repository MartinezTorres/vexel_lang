## Stdout
```
// Lowered Vexel module: tests/grammar/GR-057/constructor_qualified/test.vx
#File(name: #s);
#Rectangle(x: #i32, y: #i32, w: #i32, h: #i32);
&^main() -> #i32 {
    file = File("input.txt");
    rect = Rectangle(0, 0, 100, 100);
    rect.w
}
```

## Stderr
```
```

## Exit Code
0
