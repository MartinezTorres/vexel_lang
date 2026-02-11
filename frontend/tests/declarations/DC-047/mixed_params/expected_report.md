## Stdout
```
// Lowered Vexel module: tests/declarations/DC-047/mixed_params/test.vx
&mix($body: #T0, amount: #i32, $after: #T1) -> #i32 {
    temp = amount;
    body;
    temp = temp + 1;
    after;
    temp
}
&^main() -> #i32 {
    sum: #i32 = 0;
    other: #i32 = 5;
    mix(sum = sum + 1, 3, other = other * 2);
    sum + other
}
```

## Stderr
```
```

## Exit Code
0
