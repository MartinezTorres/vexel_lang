## Stdout
```
// Lowered Vexel module: tests/declarations/DC-109/exported_primitives/test.vx
&^compute(a: #i32, b: #f64, flag: #b) -> #i32 {
    flag ? a : ( #i32 ) 0
}
&^main() -> #i32 {
    compute(100, 3.14, ( #b ) 1)
}
```

## Stderr
```
```

## Exit Code
0
