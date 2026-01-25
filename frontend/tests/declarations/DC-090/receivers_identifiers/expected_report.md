## Stdout
```
// Lowered Vexel module: tests/declarations/DC-090/receivers_identifiers/test.vx
&(first, second)sum() -> #i32 {
    first + second
}
&^main() -> #i32 {
    a = 100;
    b = 200;
    sum()
}
```

## Stderr
```
```

## Exit Code
0
