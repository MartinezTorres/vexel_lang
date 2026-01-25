## Stdout
```
// Lowered Vexel module: tests/grammar/GR-037/postfix_call_args/test.vx
&func(x: #i32, y: #i32, z: #i32) -> #i32 {
    x + y + z
}
&^main() -> #i32 {
    result = func(1, 2, 3);
    result
}
```

## Stderr
```
```

## Exit Code
0
