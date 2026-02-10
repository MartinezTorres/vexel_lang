## Stdout
```
// Lowered Vexel module: tests/declarations/DC-083/array_fallback/test.vx
&^main() -> #i32 {
    arr = [3, 1, 2];
    sum = 0;
    arr@@{
            sum = sum + _
        };
    sum
}
```

## Stderr
```
```

## Exit Code
0
