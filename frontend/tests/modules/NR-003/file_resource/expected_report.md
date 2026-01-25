## Stdout
```
// Lowered Vexel module: tests/modules/NR-003/file_resource/test.vx
content: #s = "hello world";
&^main() -> #i32 {
    result = content == "" ? ( #i32 ) 1 : ( #i32 ) 0;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
