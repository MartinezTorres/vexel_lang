## Stdout
```
// Lowered Vexel module: tests/modules/NR-009/dir_tuple_field_0/test.vx
items: #__Tuple2_s_s[1] = [("myfile.dat", "data")];
&^main() -> #i32 {
    result = items[0].__0 == "myfile.dat" ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
