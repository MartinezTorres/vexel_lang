## Stdout
```
// Lowered Vexel module: tests/modules/NR-029/block_scoped_import/test.vx
&^main() -> #i32 {
    {
        ::helper;
        result = compute()
    };
    0
}
&compute() -> #i32 {
    777
}
```

## Stderr
```
```

## Exit Code
0
