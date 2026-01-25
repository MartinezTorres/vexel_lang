## Stdout
```
// Lowered Vexel module: tests/expressions/EX-003/argument_evaluation_order/test.vx
&!print(arg0: #i32);
&get_first() -> #i32 {
    print(1);
    10
}
&get_second() -> #i32 {
    print(2);
    20
}
&get_third() -> #i32 {
    print(3);
    30
}
&sum(a: #i32, b: #i32, c: #i32) -> #i32 {
    a + b + c
}
&^main() -> #i32 {
    result = sum(get_first(), get_second(), get_third());
    0
}
```

## Stderr
```
```

## Exit Code
0
