## Stdout
```
// Lowered Vexel module: tests/declarations/DC-117/type_constructor_syntax/test.vx
#Person(name: #u8[32], age: #i32);
&^main() -> #i32 {
    p = Person([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], 25);
    p.age
}
```

## Stderr
```
```

## Exit Code
0
