## Stdout
```
// Lowered Vexel module: tests/declarations/DC-067/operator_one_receiver/test.vx
#Operand(val: #i32);
&(op)#Operand::+(rhs: #Operand) -> #Operand {
    Operand(op.val + rhs.val)
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
