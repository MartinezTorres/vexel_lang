## Stdout
```
// Lowered Vexel module: tests/expressions/EX-004/dispatch_after_evaluation/test.vx
&!print(arg0: #i32);
&get_value(x: #i32) -> #i32 {
    print(x);
    x * 2
}
&compute(a: #i32, b: #i32) -> #i32 {
    print(100);
    a + b
}
&^main() -> #i32 {
    result = compute(get_value(1), get_value(2));
    0
}
```

## Stderr
```
```

## Exit Code
0
