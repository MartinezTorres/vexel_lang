## Stdout
```
// Lowered Vexel module: tests/lexer/LX-023/sigil_at/test.vx
&^main() -> #i32 {
    total = 0;
    2..5@{
            total = total + _
        };
    total
}
```

## Stderr
```
```

## Exit Code
0
