## Stdout
```
// Lowered Vexel module: tests/modules/NR-020/compile_time_loading/test.vx
MANIFEST: #s = "version=1.0";
&^main() -> #i32 {
    result = MANIFEST == "version=1.0" ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
