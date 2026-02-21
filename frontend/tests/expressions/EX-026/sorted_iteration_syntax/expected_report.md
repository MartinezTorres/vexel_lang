## Stdout
```
// Lowered Vexel module: tests/expressions/EX-026/sorted_iteration_syntax/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr = [30, 10, 40, 20];
    arr@@{
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
