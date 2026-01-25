## Stdout
```
// Lowered Vexel module: tests/grammar/GR-049/string_escapes/test.vx
&test() -> #s {
    s1 = "line1
line2";
    s2 = "tab	here";
    s3 = "quote: "text"";
    s4 = "backslash: \";
    s5 = "hex: AB"
}
```

## Stderr
```
```

## Exit Code
0
