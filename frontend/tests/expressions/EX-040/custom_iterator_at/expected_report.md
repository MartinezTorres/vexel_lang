## Stdout
```
// Lowered Vexel module: tests/expressions/EX-040/custom_iterator_at/test.vx
#Range(start: #i32, end: #i32);
&(self)#Range::@($loop: #T0) -> #T0 {
    _ = self.start;
    loop
}
&print(arg0: #i32) {
}
&^main() -> #i32 {
    r = Range(0, 5);
    Range::@(print(_));
    -> 0;
}
```

## Stderr
```
```

## Exit Code
0
