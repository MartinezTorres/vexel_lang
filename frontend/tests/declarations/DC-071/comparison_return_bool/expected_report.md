## Stdout
```
// Lowered Vexel module: tests/declarations/DC-071/comparison_return_bool/test.vx
#Comparable(v: #i32);
&(a)#Comparable::==(b: #Comparable) -> #b {
    a.v == b.v
}
&(a)#Comparable::<(b: #Comparable) -> #b {
    a.v < b.v
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
