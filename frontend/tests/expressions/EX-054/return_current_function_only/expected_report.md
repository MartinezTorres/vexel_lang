## Stdout
```
// Lowered Vexel module: tests/expressions/EX-054/return_current_function_only/test.vx
&!print(arg0: #i32);
&inner() -> #i32 {
    -> 42;
}
&outer() -> #i32 {
    result = inner();
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
