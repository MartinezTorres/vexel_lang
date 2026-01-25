## Stdout
```
// Lowered Vexel module: tests/declarations/DC-051/receiver_lvalue/test.vx
#Box(val: #i32);
&(b)#Box::set(new_val: #i32) -> #i32 {
    b.val = new_val
}
&^main() -> #i32 {
    box = Box(10);
    Box::set(20);
    box.val
}
```

## Stderr
```
```

## Exit Code
0
