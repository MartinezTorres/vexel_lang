## Stdout
```
// Lowered Vexel module: tests/declarations/DC-050/receiver_by_ref/test.vx
#Counter(value: #i32);
&(c)#Counter::increment() -> #i32 {
    c.value = c.value + 1
}
&^main() -> #i32 {
    cnt = Counter(0);
    Counter::increment();
    cnt.value
}
```

## Stderr
```
```

## Exit Code
0
