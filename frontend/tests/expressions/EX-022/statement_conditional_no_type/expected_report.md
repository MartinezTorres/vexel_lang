## Stdout
```
// Lowered Vexel module: tests/expressions/EX-022/statement_conditional_no_type/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    x = 10;
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
