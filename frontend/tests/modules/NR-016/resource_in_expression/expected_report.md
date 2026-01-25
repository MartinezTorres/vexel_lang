## Stdout
```
// Lowered Vexel module: tests/modules/NR-016/resource_in_expression/test.vx
&^main() -> #i32 {
    val = "42";
    result = val == "42" ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
