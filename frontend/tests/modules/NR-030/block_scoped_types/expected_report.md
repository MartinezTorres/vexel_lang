## Stdout
```
// Lowered Vexel module: tests/modules/NR-030/block_scoped_types/test.vx
&^main() -> #i32 {
    {
        #LocalType(x: #i32);
        val = LocalType(42)
    };
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
