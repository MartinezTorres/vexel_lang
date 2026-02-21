## Stdout
```
// Lowered Vexel module: tests/declarations/DC-086/custom_user_iter/test.vx
#UserType(values: #i32[2]);
&(u)#UserType::@($loop: #T0) -> #T0 {
    _ = u.values[0];
    loop
}
&^main() -> #i32 {
    ut = UserType([100, 200]);
    total = 0;
    UserType::@({
        total = total + _
    });
    total
}
```

## Stderr
```
```

## Exit Code
0
