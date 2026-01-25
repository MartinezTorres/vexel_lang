## Stdout
```
// Lowered Vexel module: tests/declarations/DC-062/operator_left_ref/test.vx
#Accumulator(sum: #i32);
&(a)#Accumulator::+(val: #i32) -> #Accumulator {
    a.sum = a.sum + val;
    a
}
&^main() -> #i32 {
    acc = Accumulator(0);
    acc = Accumulator::+(10);
    acc.sum
}
```

## Stderr
```
```

## Exit Code
0
