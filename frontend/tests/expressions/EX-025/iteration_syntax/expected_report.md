## Stdout
```
// Lowered Vexel module: tests/expressions/EX-025/iteration_syntax/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr = [10, 20, 30];
    arr@{
        print(1)
    };
    0
}
```

## Stderr
```
```

## Exit Code
0
