## Stdout
```
// Lowered Vexel module: tests/declarations/DC-063/operator_left_identifier/test.vx
#Value(n: #i32);
&(v)#Value::+(other: #Value) -> #Value {
    Value(v.n + other.n)
}
&^main() -> #i32 {
    a = Value(5);
    b = Value(10);
    c = Value::+(b);
    c.n
}
```

## Stderr
```
```

## Exit Code
0
