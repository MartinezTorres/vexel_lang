## Stdout
```
// Lowered Vexel module: tests/declarations/DC-046/instantiation_typecheck/test.vx
&process($val: #T0) -> #i32 {
    x = val
}
&^main() -> #i32 {
    process(42);
    0
}
```

## Stderr
```
```

## Exit Code
0
