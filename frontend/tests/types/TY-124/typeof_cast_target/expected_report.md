## Stdout
```
// Lowered Vexel module: tests/types/TY-124/typeof_cast_target/test.vx
&!input() -> #u8;
&^main() -> #i32 {
    x = input();
    y = ( #u8 ) x;
    ( #i32 ) y
}
```

## Stderr
```
```

## Exit Code
0
