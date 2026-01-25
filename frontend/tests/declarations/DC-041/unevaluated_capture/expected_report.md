## Stdout
```
// Lowered Vexel module: tests/declarations/DC-041/unevaluated_capture/test.vx
&twice($action: #T0) -> #T0 {
    action;
    action
}
&^main() -> #i32 {
    x = 0;
    twice(x = x + 1);
    x
}
```

## Stderr
```
```

## Exit Code
0
