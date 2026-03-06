# 1. Lexical and Tokens

RFC references: [Lexical & Tokens](../vexel-rfc.md#lexical--tokens), [Grammar](../vexel-rfc.md#grammar)

## 1.1 Character Set and Basic Input Rules

- Source is ASCII-7 only.
- Whitespace recognized by the lexer: space, tab, carriage return, line feed.
- Comments begin with `//` and continue to end of line.

## 1.2 Identifiers

- Identifier pattern: `[A-Za-z_][A-Za-z0-9_]*`.
- Primitive names are not reserved as standalone identifiers.
- Primitive spellings (`iN`, `uN`, `f16`, `f32`, `f64`, `b`, `s`) are only special after `#`.

Example:

```vx
b:#i32 = 7;    // valid variable name
flag:#b = 1;   // #b is primitive bool type
```

## 1.3 Literals

### Integers

- Decimal and hex forms are accepted (`123`, `0xFF`, `0XFF`).
- Integer literals are initially unresolved exact integers.
- Concrete type is inferred from context and representability checks.

### Floating literals

- Decimal form with optional exponent (`1.0`, `1.23e4`, `1.23E-4`).
- Default floating type is `#f64` when unconstrained.

### Char and string literals

- Char literal: `'a'`, defaulting to `#u8`.
- String literal: `"..."`, type `#s`.
- Supported escapes: `\n`, `\r`, `\t`, `\\`, `\"`, `\xHH`, octal `\N`, `\NN`, `\NNN`.

## 1.4 Sigils and Token Classes

Core sigils:

- `$` expression parameter
- `@` iteration
- `&` function declaration
- `&!` external function import
- `&^` exported function
- `^` exported global
- `!` external global symbol
- `!!` backend-bound global
- `#` type namespace and type syntax

## 1.5 Operators

The language surface includes arithmetic, bitwise, comparison, logical, assignment, control, and dotted per-element variants.

Important control tokens are lexical units:

- `->` return
- `->|` break
- `->>` continue

Spacing-sensitive note:

- `->|` and `->>` are single control tokens.
- Spaced forms like `-> |` and `-> >` are not loop-control statements.

## 1.6 Statement Termination

Statements terminate with `;` or with whitespace when grammar is unambiguous.

Ambiguous contexts (for example when two parses compete) require explicit terminators/parentheses.

## 1.7 Annotation Lexing Surface

Annotation tokens use:

- `[[name]]`
- `[[name(arg1, arg2)]]`

Lexically they are regular tokens. Semantic ownership is backend-side; frontend parses/preserves annotation payloads.
