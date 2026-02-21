## Stdout
```
// Lowered Vexel module: tests/expressions/EX-047/repeat_no_underscore/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    i = 0;
    i < 3@{
            print(i);
            i = i + 1
        };
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
