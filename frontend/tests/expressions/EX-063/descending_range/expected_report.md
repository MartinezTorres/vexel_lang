## Stdout
```
// Lowered Vexel module: tests/expressions/EX-063/descending_range/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    5..0@{
        print(_);
    };
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
