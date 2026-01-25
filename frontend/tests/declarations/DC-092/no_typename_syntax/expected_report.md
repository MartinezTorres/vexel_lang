## Stdout
```
// Lowered Vexel module: tests/declarations/DC-092/no_typename_syntax/test.vx
&(x, y)process() -> #i32 {
    x + y
}
&^main() -> #i32 {
    a = 1;
    b = 2;
    process()
}
```

## Stderr
```
```

## Exit Code
0
