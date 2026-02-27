## Stdout
```
// Lowered Vexel module: tests/expressions/EX-108/receiver_call_formatting/test.vx
#Vec(x: #i32);
&(self)#Vec::len() -> #i32 {
    self.x
}
&!seed1() -> #i32;
&!seed2() -> #i32;
&^main() -> #i32 {
    a = #Vec(seed1());
    b = #Vec(seed2());
    a.len() + b.len()
}
```

## Stderr
```
```

## Exit Code
0
