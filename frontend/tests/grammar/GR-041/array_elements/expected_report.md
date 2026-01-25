## Stdout
```
// Lowered Vexel module: tests/grammar/GR-041/array_elements/test.vx
&^main() -> #i32 {
    nums = [1, 2, 3, 4, 5];
    mixed = [1, 2 + 3, 4];
    nums[0] + mixed[1]
}
```

## Stderr
```
```

## Exit Code
0
