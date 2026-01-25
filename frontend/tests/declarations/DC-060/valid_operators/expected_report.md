## Stdout
```
// Lowered Vexel module: tests/declarations/DC-060/valid_operators/test.vx
#Num(v: #i32);
&(l)#Num::+(r: #Num) -> #Num {
    Num(l.v + r.v)
}
&(l)#Num::-(r: #Num) -> #Num {
    Num(l.v - r.v)
}
&(l)#Num::*(r: #Num) -> #Num {
    Num(l.v * r.v)
}
&(l)#Num::==(r: #Num) -> #b {
    l.v == r.v
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
