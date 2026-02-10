## Stdout
```
// Lowered Vexel module: tests/typechecker/TC-CONCRETE-USAGE/constexpr_dead_stmt_branch_ok/test.vx
&!foo();
&^main() -> #i32 {
    0 ? 
        {
            tmp = foo()
        };
    0
}
```

## Stderr
```
```

## Exit Code
0
