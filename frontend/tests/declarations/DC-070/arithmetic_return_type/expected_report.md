## Stdout
```
// Lowered Vexel module: tests/declarations/DC-070/arithmetic_return_type/test.vx
#Arithmetic(val: #i32);
&(a)#Arithmetic::+(b: #Arithmetic) -> #Arithmetic {
    Arithmetic(a.val + b.val)
}
&(a)#Arithmetic::*(b: #Arithmetic) -> #Arithmetic {
    Arithmetic(a.val * b.val)
}
&^main() -> #i32 {
    0
}
```

## Stderr
```
```

## Exit Code
0
