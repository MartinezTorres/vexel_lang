## Stdout
```
// Lowered Vexel module: tests/declarations/DC-014/init_before_use/test.vx
counter: #i32;
&^main() -> #i32 {
    counter = 0;
    counter = counter + 1;
    counter
}
```

## Stderr
```
```

## Exit Code
0
