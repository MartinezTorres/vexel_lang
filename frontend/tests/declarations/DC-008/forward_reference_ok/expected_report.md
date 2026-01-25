## Stdout
```
// Lowered Vexel module: tests/declarations/DC-008/forward_reference_ok/test.vx
FIRST: #i8 = 10;
SECOND: #i8 = FIRST + 5;
&^main() -> #i32 {
    SECOND
}
```

## Stderr
```
```

## Exit Code
0
