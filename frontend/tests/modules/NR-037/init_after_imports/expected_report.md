## Stdout
```
// Lowered Vexel module: tests/modules/NR-037/init_after_imports/test.vx
::dependency;
MY_VALUE: #i32 = INIT_VALUE + 1;
&^main() -> #i32 {
    result = MY_VALUE == 101 ? ( #i32 ) 0 : ( #i32 ) 1;
    -> result;
}
INIT_VALUE: #i32 = 100;
```

## Stderr
```
```

## Exit Code
0
