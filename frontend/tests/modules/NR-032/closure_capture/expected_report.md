## Stdout
```
// Lowered Vexel module: tests/modules/NR-032/closure_capture/test.vx
&^main() -> #i32 {
    total = 0;
    &add_one() -> #i32 {
        total = total + 1;
        total
    }
    &add_two() -> #i32 {
        total = total + 2;
        total
    }
    add_one();
    add_two();
    add_one();
    total
}
```

## Stderr
```
```

## Exit Code
0
