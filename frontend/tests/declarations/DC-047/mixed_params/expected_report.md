## Stdout
```
// Lowered Vexel module: tests/declarations/DC-047/mixed_params/test.vx
&^main() -> #i32 {
    sum = 0;
    other = 5;
    mix(sum = sum + 1, 3, other = other * 2);
    sum + other
}
```

## Stderr
```
```

## Exit Code
0
