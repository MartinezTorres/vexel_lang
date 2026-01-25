## Stdout
```
// Lowered Vexel module: tests/expressions/EX-053/return_without_value/test.vx
&!print(arg0: #i32);
&maybe_print(flag: #b, value: #i32) -> #T0 {
    !flag ? 
        ->;
    print(value)
}
&^main() -> #i32 {
    maybe_print(0, 10);
    maybe_print(1, 20);
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
