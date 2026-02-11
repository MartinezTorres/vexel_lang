## Stdout
```
// Lowered Vexel module: tests/declarations/DC-055/dot_notation/test.vx
#Item(value: #i32);
&(i)#Item::get_value() -> #i32 {
    i.value
}
&^main() -> #i32 {
    item = Item(99);
    Item::get_value()
}
```

## Stderr
```
```

## Exit Code
0
