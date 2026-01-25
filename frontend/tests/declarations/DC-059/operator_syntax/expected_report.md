## Stdout
```
// Lowered Vexel module: tests/declarations/DC-059/operator_syntax/test.vx
#Number(val: #i32);
&(lhs)#Number::+(rhs: #Number) -> #Number {
    Number(lhs.val + rhs.val)
}
&^main() -> #i32 {
    a = Number(10);
    b = Number(20);
    c = Number::+(b);
    c.val
}
```

## Stderr
```
```

## Exit Code
0
