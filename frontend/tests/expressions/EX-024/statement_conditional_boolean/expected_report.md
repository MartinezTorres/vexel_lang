## Stdout
```
// Lowered Vexel module: tests/expressions/EX-024/statement_conditional_boolean/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    flag = true;
    flag ? 
        print(1);
    0
}
```

## Stderr
```
```

## Exit Code
0
