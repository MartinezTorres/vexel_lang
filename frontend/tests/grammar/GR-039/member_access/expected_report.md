## Stdout
```
// Lowered Vexel module: tests/grammar/GR-039/member_access/test.vx
#Point(x: #i32, y: #i32);
&^main() -> #i32 {
    p = Point(1, 2);
    x = p.x;
    x
}
```

## Stderr
```
```

## Exit Code
0
