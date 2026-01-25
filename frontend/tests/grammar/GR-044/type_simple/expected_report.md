## Stdout
```
// Lowered Vexel module: tests/grammar/GR-044/type_simple/test.vx
#MyType(v: #i32);
&^main() -> #i32 {
    x = 1;
    y = 2.0;
    z = MyType(3);
    x + z.v + ( #i32 ) y
}
```

## Stderr
```
```

## Exit Code
0
