## Stdout
```
// Lowered Vexel module: tests/expressions/EX-087/assignment_right_to_left/test.vx
&!print(arg0: #i32);
&get_value() -> #i32 {
    print(99);
    -> 10;
}
&^main() -> #i32 {
    a: #i32 = 0;
    b: #i32 = 0;
    a = b = get_value();
    -> a + b;
}
```

## Stderr
```
```

## Exit Code
0
