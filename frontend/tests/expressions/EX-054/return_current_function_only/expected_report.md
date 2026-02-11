## Stdout
```
// Lowered Vexel module: tests/expressions/EX-054/return_current_function_only/test.vx
&!print(arg0: #i32);
&outer() -> #i32 {
    result = 42;
    print(result);
    -> 0;
}
&^main() -> #i32 {
    -> outer();
}
```

## Stderr
```
```

## Exit Code
0
