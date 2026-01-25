## Stdout
```
// Lowered Vexel module: tests/modules/NR-007/dir_lexicographic_order/test.vx
files: #__Tuple2_s_s[3] = [("aaa.txt", "first"), ("bbb.txt", "second"), ("zzz.txt", "third")];
&^main() -> #i32 {
    result = files[0].__0 == "aaa.txt" && files[2].__0 == "zzz.txt" ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
