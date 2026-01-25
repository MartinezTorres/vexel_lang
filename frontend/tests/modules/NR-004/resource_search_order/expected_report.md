## Stdout
```
// Lowered Vexel module: tests/modules/NR-004/resource_search_order/test.vx
config: #s = "project-root-config";
&^main() -> #i32 {
    result = config == "project-root-config" ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
