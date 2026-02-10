## Stdout
```
// Lowered Vexel module: tests/modules/NR-035/no_shadowing_reject/test.vx
x: #i32 = 10;
&^main() -> #i32 {
    x = 20;
    x
}
```

## Stderr
```
```

## Exit Code
0
