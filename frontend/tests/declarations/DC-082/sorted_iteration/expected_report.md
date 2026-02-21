## Stdout
```
// Lowered Vexel module: tests/declarations/DC-082/sorted_iteration/test.vx
#SortedSet(vals: #i32[3]);
&(s)#SortedSet::@@($loop: #T0) -> #T0 {
    _ = s.vals[0];
    loop
}
&^main() -> #i32 {
    set = SortedSet([1, 2, 3]);
    result = 0;
    SortedSet::@@({
        result = result + _
    });
    result
}
```

## Stderr
```
```

## Exit Code
0
