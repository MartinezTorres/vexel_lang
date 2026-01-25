## Stdout
```
// Lowered Vexel module: tests/types/TY-046/type_constructor_function/test.vx
#Color(r: #u8, g: #u8, b: #u8);
&^main() -> #i32 {
    c = Color(255, 128, 64);
    ( #i32 ) c.r
}
```

## Stderr
```
```

## Exit Code
0
