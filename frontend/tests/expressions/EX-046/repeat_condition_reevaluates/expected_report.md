## Stdout
```
// Lowered Vexel module: tests/expressions/EX-046/repeat_condition_reevaluates/test.vx
&!print(arg0: #i32);
&^main() -> #i32 {
    count = 0;
    count < 3@{{
            print(count);
            count = count + 1
        }};
    -> count;
}
```

## Stderr
```
```

## Exit Code
0
