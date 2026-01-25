## Stdout
```
// Lowered Vexel module: tests/declarations/DC-069/operator_return_normal/test.vx
#Result(r: #i32);
&(a)#Result::+(b: #Result) -> #Result {
    Result(a.r + b.r)
}
&^main() -> #i32 {
    0
}
```

## Stderr
```
```

## Exit Code
0
