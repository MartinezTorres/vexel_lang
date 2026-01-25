## Stdout
```
// Lowered Vexel module: tests/expressions/EX-015/different_types_compile_time/test.vx
FEATURE_ENABLED: #b = 1;
&^main() -> #i32 {
    result = FEATURE_ENABLED ? 42 : "disabled";
    -> result;
}
```

## Stderr
```
```

## Exit Code
0
