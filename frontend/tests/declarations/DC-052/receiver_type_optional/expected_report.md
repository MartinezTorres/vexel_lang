## Stdout
```
// Lowered Vexel module: tests/declarations/DC-052/receiver_type_optional/test.vx
#Thing(data: #i32);
&(t)#Thing::get() -> #i32 {
    t.data
}
&^main() -> #i32 {
    thing = Thing(42);
    Thing::get()
}
```

## Stderr
```
```

## Exit Code
0
