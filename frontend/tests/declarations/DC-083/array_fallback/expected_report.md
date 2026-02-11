## Stdout
```
// Lowered Vexel module: tests/declarations/DC-083/array_fallback/test.vx
&^main() -> #i32 {
    arr: #i32[3] = [3, 1, 2];
    sum: #i32 = 0;
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
