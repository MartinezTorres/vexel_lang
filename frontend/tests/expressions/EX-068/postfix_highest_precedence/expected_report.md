## Stdout
```
// Lowered Vexel module: tests/expressions/EX-068/postfix_highest_precedence/test.vx
#Point(x: #i32, y: #i32);
&^main() -> #i32 {
    arr = [Point(1, 2), Point(3, 4)];
    result = arr[0].x;
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
