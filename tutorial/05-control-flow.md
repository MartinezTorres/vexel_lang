# Control Flow and Iteration

Vexel has **no keywords**, so control flow uses operators.

### Conditionals

Expression conditional (yields a value):

```vexel
x:#i32 = 7;
parity:#b = (x % 2) == 0 ? 1 : 0;
```

Statement conditional (no else branch):

```vexel
x == 0 ? {
  x = 1
};
```

### Iteration (`@` and `@@`)

`iterable@expr` loops over arrays or ranges and binds `_` to each element:

```vexel
arr:#i32[4] = [1, 2, 3, 4];
sum:#i32 = 0;

arr@{
  sum = sum + _
};
```

`@@` iterates over a **sorted copy** of the array:

```vexel
arr@@{ /* _ is visited in sorted order */ };
```

### Ranges

Ranges are array literals in disguise:

```vexel
rng = 0..5;   // [0, 1, 2, 3, 4]
```

### Repeat loop

`(cond)@expr` repeats while the condition is true:

```vexel
i:#i32 = 3;
(i > 0)@{
  i = i - 1
};
```

### Loop control

- `->|;` breaks the innermost loop
- `->>;` continues the innermost loop

```vexel
arr@{
  (_ == 3) ? ->|;
  sum = sum + _
};
```

- Prev: [Functions and Compile-Time](04-functions-and-compile-time.md)
- Next: [Structs and Methods](06-structs-and-methods.md)
