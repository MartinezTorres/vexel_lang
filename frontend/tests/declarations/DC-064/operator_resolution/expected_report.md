## Stdout
```
// Lowered Vexel module: tests/declarations/DC-064/operator_resolution/test.vx
&^main() -> #i32 {
    x = Integer(100);
    y = Integer(200);
    z = Integer::+(y);
    z.i
}
```

## Stderr
```
```

## Exit Code
0
