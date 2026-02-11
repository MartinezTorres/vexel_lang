## Stdout
```
// Lowered Vexel module: tests/declarations/DC-057/receiver_identifier/test.vx
#Data(x: #i32);
&(d)#Data::process() -> #i32 {
    d.x
}
&^main() -> #i32 {
    data = Data(55);
    Data::process()
}
```

## Stderr
```
```

## Exit Code
0
