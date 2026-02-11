## Stdout
```
// Lowered Vexel module: tests/expressions/EX-021/statement_conditional_syntax/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    x: #i32 = 10;
    x > 5 ? 
        print(x);
    0
}
```

## Stderr
```
```

## Exit Code
0
