## Stdout
```
// Lowered Vexel module: tests/grammar/GR-040/arglist_multiple/test.vx
&foo(a: #i32, b: #i32, c: #i32, d: #i32, e: #i32) -> #i32 {
    a + b + c + d + e
}
&^main() -> #i32 {
    arr: #i32[2] = [1, 2];
    foo(1, 2, 3 + 4, 5, arr[1])
}
```

## Stderr
```
```

## Exit Code
0
