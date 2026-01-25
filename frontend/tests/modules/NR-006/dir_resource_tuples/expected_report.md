## Stdout
```
// Lowered Vexel module: tests/modules/NR-006/dir_resource_tuples/test.vx
files: #__Tuple2_s_s[2] = [("file1.txt", "content1"), ("file2.txt", "content2")];
&^main() -> #i32 {
    result = files[0].__0 == "" || files[0].__1 == "" ? ( #i32 ) 1 : ( #i32 ) 0;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
