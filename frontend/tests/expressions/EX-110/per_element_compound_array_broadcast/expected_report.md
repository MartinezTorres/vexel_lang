## Stdout
```
// Lowered Vexel module: tests/expressions/EX-110/per_element_compound_array_broadcast/test.vx
#Box(v: #i32);
&(lhs)#Box::.+(rhs: #Box) -> #Box {
    Box(lhs.v + rhs.v)
}
&!seed() -> #i32;
&!tick() -> #b;
&^main() -> #i32 {
    base = seed();
    pulse = tick();
    one = 1;
    xs = [1, 2];
    flags = [0, pulse];
    boxes = [Box(base), Box(base + 1)];
    off = Box(10);
    xs = [xs[0] + base, xs[1] + base];
    flags = [flags[0] || one, flags[1] || one];
    boxes = [boxes[0] .+ off, boxes[1] .+ off];
    ( #i32 ) xs[0] + ( #i32 ) xs[1] + ( #i32 ) flags[0] + ( #i32 ) flags[1] + boxes[1].v
}
```

## Stderr
```
```

## Exit Code
0
