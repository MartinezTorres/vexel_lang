## Stdout
```
// Lowered Vexel module: tests/modules/NR-025/reimport_identical/test.vx
::utils;
::utils;
&^main() -> #i32 {
    add(1, 2)
}
&add(a: #i32, b: #i32) -> #i32 {
    a + b
}
VALUE: #i32 = 42;
```

## Stderr
```
```

## Exit Code
0
