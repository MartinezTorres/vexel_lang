## Stdout
```
// Lowered Vexel module: tests/inference/TI-014/nested_modify/test.vx
&^main() -> #i32 {
    counter = 0;
    &increment() -> #i32 {
        counter = counter + 1;
        counter
    }
    increment();
    increment();
    increment();
    counter
}
```

## Stderr
```
```

## Exit Code
0
