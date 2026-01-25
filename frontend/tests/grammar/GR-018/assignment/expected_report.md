## Stdout
```
// Lowered Vexel module: tests/grammar/GR-018/assignment/test.vx
#Point(x: #i32, y: #i32);
&^main() -> #i32 {
    mut x: #i32 = 0;
    mut arr: #i32[2] = [0, 0];
    mut obj: #Point = Point(0, 0);
    x = 10;
    arr[0] = 20;
    obj.x = 30;
    0
}
```

## Stderr
```
```

## Exit Code
0
