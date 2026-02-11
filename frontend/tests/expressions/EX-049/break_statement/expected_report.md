## Stdout
```
// Lowered Vexel module: tests/expressions/EX-049/break_statement/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    i: #i32 = 0;
    i < 10@{
            i == 5 ? 
                ->|;
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
