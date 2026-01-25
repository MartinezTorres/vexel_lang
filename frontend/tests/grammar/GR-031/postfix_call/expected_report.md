## Stdout
```
// Lowered Vexel module: tests/grammar/GR-031/postfix_call/test.vx
&helper(x: #i32) -> #i32 {
    x + 1
}
&^main() -> #i32 {
    result = helper(3);
    result
}
```

## Stderr
```
```

## Exit Code
0
