## Stdout
```
// Lowered Vexel module: tests/modules/NR-036/underscore_shadow_allowed/test.vx
&^main() -> #i32 {
    arr1 = [1, 2, 3];
    arr1@{
            _ + 0;
        };
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
