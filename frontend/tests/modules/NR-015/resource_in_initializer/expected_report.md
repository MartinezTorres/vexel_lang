## Stdout
```
// Lowered Vexel module: tests/modules/NR-015/resource_in_initializer/test.vx
CONFIG: #s = "{"key": "value"}";
&^main() -> #i32 {
    result = CONFIG == "" ? ( #i32 ) 1 : ( #i32 ) 0;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
