# Modules and Resources

### Imports

Modules are imported with `::` paths:

```vexel
::path::to::module;
```

Resolution order:

1. Project root (`path/to/module.vx`)
2. Relative to the importing file

That is why files inside `examples/` can do:

```vexel
::lib::print;
```

When the importer is in `examples/`, the compiler falls back to `examples/lib/print.vx` if `lib/print.vx` does not exist at the project root.

Example usage with the bundled print helpers:

```vexel
::lib::print;

&^main() -> #i32 {
  print_str("hi");
  print_newline();
  0
}
```

### Resource expressions (advanced)

You can embed files or directories at compile time using the same `::` path syntax in an expression:

```vexel
font:#s = ::assets::font.bin;
files = ::assets::tiles; // array of (name, contents) tuples
```

Resource imports are immutable compile-time constants.

- Prev: [Structs and Methods](06-structs-and-methods.md)
- Next: [Next Steps](08-next-steps.md)
