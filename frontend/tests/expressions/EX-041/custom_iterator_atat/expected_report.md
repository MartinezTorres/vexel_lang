## Stdout
```
// Lowered Vexel module: tests/expressions/EX-041/custom_iterator_atat/test.vx
#ReverseRange(start: #i32, end: #i32);
&(self)#ReverseRange::@@($loop: #T0) -> #T0 {
    _ = self.end - 1;
    loop
}
&print(arg0: #i32) {
    0
}
&^main() -> #i32 {
    r = ReverseRange(0, 5);
    ReverseRange::@@(print(_));
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
