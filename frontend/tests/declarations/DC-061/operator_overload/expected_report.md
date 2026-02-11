## Stdout
```
// Lowered Vexel module: tests/declarations/DC-061/operator_overload/test.vx
#Vec(x: #i32, y: #i32);
&(a)#Vec::+(b: #Vec) -> #Vec {
    Vec(a.x + b.x, a.y + b.y)
}
&^main() -> #i32 {
    v1 = Vec(1, 2);
    v2 = Vec(3, 4);
    v3 = Vec::+(v2);
    v3.x + v3.y
}
```

## Stderr
```
```

## Exit Code
0
