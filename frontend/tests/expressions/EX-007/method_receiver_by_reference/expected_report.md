## Stdout
```
// Lowered Vexel module: tests/expressions/EX-007/method_receiver_by_reference/test.vx
#Counter(value: #i32);
&(self)#Counter::increment() -> #i32 {
    self.value = self.value + 1
}
&^main() -> #i32 {
    c = #Counter(0);
    c.increment();
    c.increment();
    -> c.value;
}
```

## Stderr
```
```

## Exit Code
0
