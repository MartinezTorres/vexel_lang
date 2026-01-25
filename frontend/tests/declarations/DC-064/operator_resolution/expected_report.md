## Stdout
```
// Lowered Vexel module: tests/declarations/DC-064/operator_resolution/test.vx
#Integer(i: #i32);
&(a)#Integer::+(b: #Integer) -> #Integer {
    Integer(a.i + b.i)
}
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
