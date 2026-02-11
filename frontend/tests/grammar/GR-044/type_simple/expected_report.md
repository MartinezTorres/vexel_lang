## Stdout
```
// Lowered Vexel module: tests/grammar/GR-044/type_simple/test.vx
&^main() -> #i32 {
    x: #i32 = 1;
    y: #f32 = 2.0;
    z = MyType(3);
    x + z.v + ( #i32 ) y
}
```

## Stderr
```
```

## Exit Code
0
