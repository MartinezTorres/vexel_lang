## Stdout
```
// Lowered Vexel module: tests/expressions/EX-002/receiver_evaluation_order/test.vx
&!print(arg0: #i32);
&get_func_id() -> #i32 {
    print(1);
    10
}
&get_arg() -> #i32 {
    print(2);
    20
}
&process(x: #i32) -> #i32 {
    x
}
&^main() -> #i32 {
    result = process(get_arg());
    0
}
```

## Stderr
```
```

## Exit Code
0
