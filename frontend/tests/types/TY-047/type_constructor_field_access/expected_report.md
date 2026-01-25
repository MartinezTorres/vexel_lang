## Stdout
```
// Lowered Vexel module: tests/types/TY-047/type_constructor_field_access/test.vx
#Rect(w: #i32, h: #i32);
&^main() -> #i32 {
    r = Rect(100, 50);
    r.w * r.h
}
```

## Stderr
```
```

## Exit Code
0
