## Stdout
```
// Lowered Vexel module: tests/grammar/GR-054/func_call_qualified/test.vx
&println(msg: #s) {
}
&func(x: #i32, y: #f32) {
}
&^main() -> #i32 {
    println("Hello");
    func(42, 3.14);
    0
}
```

## Stderr
```
```

## Exit Code
0
