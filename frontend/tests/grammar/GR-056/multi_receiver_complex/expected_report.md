## Stdout
```
// Lowered Vexel module: tests/grammar/GR-056/multi_receiver_complex/test.vx
&(first, second, third)merge(x: #i32, y: #i32, z: #i32) -> #i32 {
    first + second + third + x + y + z
}
&^main() -> #i32 {
    obj1: #i32 = 1;
    obj2: #i32 = 2;
    obj3: #i32 = 3;
    merge(4, 5, 6)
}
```

## Stderr
```
```

## Exit Code
0
