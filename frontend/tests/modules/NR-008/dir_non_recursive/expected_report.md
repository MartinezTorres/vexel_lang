## Stdout
```
// Lowered Vexel module: tests/modules/NR-008/dir_non_recursive/test.vx
files: #__Tuple2_s_s[1] = [("top.txt", "top-level")];
&^main() -> #i32 {
    len = |files|;
    result = len == 1 && files[0].__0 == "top.txt" ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
