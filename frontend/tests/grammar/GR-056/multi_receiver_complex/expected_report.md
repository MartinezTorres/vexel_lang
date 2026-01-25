## Stdout
```
// Lowered Vexel module: tests/grammar/GR-056/multi_receiver_complex/test.vx
&(first, second, third)merge(x: #i32, y: #i32, z: #i32) -> #i32 {
    first + second + third + x + y + z
}
&(a, b)combine(p: #i32, q: #i32, r: #i32, s: #i32) -> #i32 {
    a + b + p + q + r + s
}
&^main() -> #i32 {
    obj1 = 1;
    obj2 = 2;
    obj3 = 3;
    merge(4, 5, 6)
}
```

## Stderr
```
```

## Exit Code
0
