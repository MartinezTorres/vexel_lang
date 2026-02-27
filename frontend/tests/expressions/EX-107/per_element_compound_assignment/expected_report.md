## Stdout
```
// Lowered Vexel module: tests/expressions/EX-107/per_element_compound_assignment/test.vx
#Box(v: #i32);
#Flag(v: #b);
&(lhs)#Box::.+(rhs: #Box) -> #Box {
    #Box(lhs.v + rhs.v)
}
&(lhs)#Flag::.||(rhs: #Flag) -> #Flag {
    #Flag(lhs.v || rhs.v)
}
&!seed() -> #i32;
&!tick() -> #b;
&^main() -> #i32 {
    b = #Box(seed());
    b = b .+ #Box(2);
    f = #Flag(1);
    f = f .|| #Flag(tick());
    b.v + ( #i32 ) f.v
}
```

## Stderr
```
```

## Exit Code
0
