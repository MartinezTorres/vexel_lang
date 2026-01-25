## Stdout
```
// Lowered Vexel module: tests/expressions/EX-050/continue_statement/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    i = 0;
    i < 5@{{
            i = i + 1;
            i == 3 ? 
                ->>;
            print(i)
        }};
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
