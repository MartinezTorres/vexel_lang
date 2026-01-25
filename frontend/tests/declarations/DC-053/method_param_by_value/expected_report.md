## Stdout
```
// Lowered Vexel module: tests/declarations/DC-053/method_param_by_value/test.vx
#Calculator(result: #i32);
&(c)#Calculator::add(val: #i32) -> #i32 {
    c.result = c.result + val
}
&^main() -> #i32 {
    calc = Calculator(0);
    Calculator::add(10);
    calc.result
}
```

## Stderr
```
```

## Exit Code
0
