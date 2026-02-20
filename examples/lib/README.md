# Vexel Standard Library

This folder contains workbench helper modules used by some examples.

## Modules

### math.vx
Minimal arithmetic helpers:
- `add(a:#i32, b:#i32) -> #i32`
- `sub(a:#i32, b:#i32) -> #i32`

### print.vx
I/O functions using external `putchar`:
- `print_char(c)`, `print_str(s)`, `print_newline()`
- `print_digit(d)`, `print_u32(n)`, `print_i32(n)`
- `print_bool(b)`

Usage:
```vexel
::lib::print;

&^main() -> #i32 {
    print_str("Hello, World!")
    print_newline()
    0
}
```

### vector.vx
Dynamic-size vector with fixed capacity:
- `#Vec(data, len, cap)` – fields: backing array, current length, capacity
- `Vec(capacity)` – construct an empty vector (caller provides backing array)
- `&(v)#Vec::push(item)`, `pop()`, `get(idx)`, `set(idx, val)`
- `&(v)#Vec::clear()`, `size()`, `is_empty()`, `is_full()`
- `&(v)#Vec::each($expr)` – iterate with an expression parameter

### stack.vx
LIFO stack implementation:
- `#Stack(data, top)` – fields: backing array, top index
- `Stack(capacity)` – construct an empty stack (caller provides backing array)
- `&(s)#Stack::push(item)`, `pop()`, `peek()`
- `&(s)#Stack::clear()`, `size()`, `is_empty()`, `is_full()`

### set.vx
Sorted set for unique elements:
- `#Set(data, len, cap)` – sorted backing array with capacity
- `Set(capacity)` – construct an empty set (caller provides backing array)
- `&(s)#Set::insert(item)`, `remove(item)`, `contains(item)`
- `&(s)#Set::clear()`, `size()`, `is_empty()`, `is_full()`
- `&(s)#Set::each($expr)` – iterate with an expression parameter

### config.vx
Simple configuration value:
- `value:#i32` – global default
- `get_value() -> #i32`

### counter.vx
Global counter helpers:
- `count:#i32` – global mutable
- `increment() -> #i32` – increments and returns the new count
- `get_count() -> #i32`

## Status

- `config.vx` and `counter.vx` compile with the current frontend.
- Other files in this folder are kept as workbench modules and may not compile under the current grammar/type rules.
