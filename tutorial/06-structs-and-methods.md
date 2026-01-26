# Structs and Methods

User-defined records are declared with a type constructor:

```vexel
#Vec2(x:#i32, y:#i32);
```

This defines a `#Vec2` type and a constructor function `Vec2(x, y)`.

```vexel
v = Vec2(1, 2);
len2:#i32 = v.x * v.x + v.y * v.y;
```

### Methods

Methods take a receiver by reference and live in a separate namespace from functions:

```vexel
&(self)#Vec2::len2() -> #i32 {
  self.x * self.x + self.y * self.y
}

&^main() -> #i32 {
  v = Vec2(3, 4);
  v.len2()
}
```

### Operator methods

You can overload operators by defining methods named after the operator:

```vexel
&(lhs)#Vec2::+(rhs:#Vec2) -> #Vec2 {
  Vec2(lhs.x + rhs.x, lhs.y + rhs.y)
}

&(lhs)#Vec2::==(rhs:#Vec2) -> #b {
  (lhs.x == rhs.x) && (lhs.y == rhs.y)
}
```

Notes:

- Types cannot be recursive (no pointers or self-referential fields).
- There is no heap allocation; all values are stack or compile-time constants.

- Prev: [Control Flow and Iteration](05-control-flow.md)
- Next: [Modules and Resources](07-modules-and-resources.md)
