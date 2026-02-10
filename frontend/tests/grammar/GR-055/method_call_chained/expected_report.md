## Stdout
```
// Lowered Vexel module: tests/grammar/GR-055/method_call_chained/test.vx
&^main() -> #i32 {
    w = Wrapper(1);
    w = Wrapper::inc();
    w = Wrapper::inc();
    w.v
}
```

## Stderr
```
```

## Exit Code
0
