## Stdout
```
// Lowered Vexel module: tests/modules/NR-010/dir_tuple_field_1/test.vx
items: #__Tuple2_s_s[1] = [("test.txt", "expected-content")];
&^main() -> #i32 {
    result = items[0].__1 == "expected-content" ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
