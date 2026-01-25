## Stdout
```
// Lowered Vexel module: tests/declarations/DC-065/operator_precedence/test.vx
#Custom(data: #i32);
&(c)#Custom::+(other: #Custom) -> #Custom {
    Custom(c.data + other.data * 2)
}
&^main() -> #i32 {
    a = Custom(1);
    b = Custom(2);
    c = Custom::+(b);
    c.data
}
```

## Stderr
```
```

## Exit Code
0
