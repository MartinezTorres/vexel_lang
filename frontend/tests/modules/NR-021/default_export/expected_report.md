## Stdout
```
// Lowered Vexel module: tests/modules/NR-021/default_export/test.vx
::lib;
&^main() -> #i32 {
    x = 42;
    VALUE = 100;
    y = VALUE;
    p = Point(1, 2);
    result = x == 42 && y == 100 && p.x == 1 && p.y == 2 ? 0 : 1;
    -> result;
}
#Point(x: #i32, y: #i32);
VALUE: #i32;
```

## Stderr
```
```

## Exit Code
0
