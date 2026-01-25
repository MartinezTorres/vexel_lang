## Stdout
```
// Lowered Vexel module: tests/expressions/EX-016/dead_branch_elimination/test.vx
ALWAYS_TRUE: #b = 1;
&^main() -> #i32 {
    result = ALWAYS_TRUE ? 10 : "this would be a type error";
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
