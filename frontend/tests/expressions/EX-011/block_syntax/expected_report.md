## Stdout
```
// Lowered Vexel module: tests/expressions/EX-011/block_syntax/test.vx
&^main() -> #i32 {
    x = {
        a: #i32 = 10;
        b: #i32 = 20;
        a + b
    };
    -> x;
}
```

## Stderr
```
```

## Exit Code
0
