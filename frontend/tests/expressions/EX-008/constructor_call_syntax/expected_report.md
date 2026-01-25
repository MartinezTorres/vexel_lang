## Stdout
```
// Lowered Vexel module: tests/expressions/EX-008/constructor_call_syntax/test.vx
#Point(x: #i32, y: #i32);
&^main() -> #i32 {
    p = Point(10, 20);
    -> p.x + p.y;
}
```

## Stderr
```
```

## Exit Code
0
