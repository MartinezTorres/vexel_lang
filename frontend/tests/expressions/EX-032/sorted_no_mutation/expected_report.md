## Stdout
```
// Lowered Vexel module: tests/expressions/EX-032/sorted_no_mutation/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    arr = [3, 1, 2];
    arr@@{
        };
    -> arr[0];
}
```

## Stderr
```
```

## Exit Code
0
