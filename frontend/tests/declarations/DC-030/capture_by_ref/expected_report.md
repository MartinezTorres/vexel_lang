## Stdout
```
// Lowered Vexel module: tests/declarations/DC-030/capture_by_ref/test.vx
&counter() -> #i32 {
    count = 0;
    &increment() -> #i32 {
        count = count + 1
    }
    increment();
    increment();
    count
}
&^main() -> #i32 {
    counter()
}
```

## Stderr
```
```

## Exit Code
0
