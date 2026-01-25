## Stdout
```
// Lowered Vexel module: tests/grammar/GR-040/arglist_multiple/test.vx
&foo(a: #i32, b: #i32, c: #i32, d: #i32, e: #i32) -> #i32 {
    a + b + c + d + e
}
&func(x: #i32) -> #i32 {
    x
}
&^main() -> #i32 {
    arr = [1, 2];
    foo(1, 2, 3 + 4, func(5), arr[1])
}
```

## Stderr
```
```

## Exit Code
0
