## Stdout
```
// Lowered Vexel module: tests/expressions/EX-077/relational_precedence/test.vx
&^main() -> #i32 {
    result = ( #u32 ) 4 | ( #u32 ) 2 < ( #u32 ) 8;
    -> result ? 1 : 0;
}
```

## Stderr
```
```

## Exit Code
0
