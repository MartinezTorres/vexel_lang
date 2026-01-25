## Stdout
```
// Lowered Vexel module: tests/lexer/LX-012/long_string/test.vx
&^main() -> #i32 {
    s = "This is a very long string literal that should be accepted by the lexer because there is no length limit according to the specification and we need to verify that it works correctly even with extremely long strings like this one that goes on and on and on and on";
    0
}
```

## Stderr
```
```

## Exit Code
0
