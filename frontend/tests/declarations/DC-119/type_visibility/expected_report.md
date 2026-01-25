## Stdout
```
// Lowered Vexel module: tests/declarations/DC-119/type_visibility/test.vx
#Global(x: #i32);
&^main() -> #i32 {
    g = Global(10);
    g.x
}
```

## Stderr
```
```

## Exit Code
0
