## Stdout
```
// Lowered Vexel module: tests/declarations/DC-034/nested_recursive/test.vx
&test() -> #i32 {
    &fib(n: #i32) -> #i32 {
        n <= 1 ? 
            -> n;
        fib(n - 1) + fib(n - 2)
    }
    fib(5)
}
&^main() -> #i32 {
    test()
}
```

## Stderr
```
```

## Exit Code
0
