## Stdout
```
// Lowered Vexel module: tests/expressions/EX-109/per_element_array_broadcast/test.vx
#Box(v: #i32);
&(lhs)#Box::.+(rhs: #Box) -> #Box {
    #Box(lhs.v + rhs.v)
}
&!seed() -> #i32;
&!tick() -> #b;
&^main() -> #i32 {
    base = seed();
    bit = tick();
    xs = [base, base + 1];
    m = [[1, 2], [3, 4]];
    row = [10, 20];
    boxes = [#Box(base), #Box(base + 1)];
    offs = [#Box(10), #Box(20)];
    a = [xs[0] + 3, xs[1] + 3];
    b = [[m[0][0] * row[0], m[0][1] * row[1]], [m[1][0] * row[0], m[1][1] * row[1]]];
    c = [a[0] == 4, a[1] == 5];
    d = [[base == base + 1, bit][0] || 1, [base == base + 1, bit][1] || 0];
    e = [boxes[0] .+ offs[0], boxes[1] .+ offs[1]];
    ( #i32 ) b[1][0] + ( #i32 ) b[1][1] + ( #i32 ) c[0] + ( #i32 ) c[1] + ( #i32 ) d[0] + ( #i32 ) d[1] + e[1].v + base
}
```

## Stderr
```
```

## Exit Code
0
