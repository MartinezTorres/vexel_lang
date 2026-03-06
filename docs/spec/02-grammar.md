# 2. Grammar and Parsing Model

RFC references: [Grammar](../vexel-rfc.md#grammar), [Declarations](../vexel-rfc.md#declarations), [Expressions & Control](../vexel-rfc.md#expressions--control)

## 2.1 Top-Level Forms

Top-level declarations include:

- function declarations (`&`, `&!`, `&^`)
- global declarations (normal, `^`, `!`, `!!`)
- type declarations (`#Name(...)`)
- module imports (`::path`)

## 2.2 Type Declaration vs Constructor Expression

`#Name(...)` appears in both declaration and expression spaces.

Rule:

- Statement-position bare `#Name(...)` parses as a type declaration.
- Constructor expression statement must be parenthesized to force expression parsing.

Example:

```vx
#Pair(a:#i32, b:#i32);

&^main() -> #i32 {
  (#Pair(1, 2));  // expression statement
  0
}
```

## 2.3 Variable Declarations

Type annotation syntax is explicit:

- valid: `x:#T`
- invalid: `x #T`

Global and local declarations follow the same annotation shape.

## 2.4 Control Statements

- `-> expr;` return value
- `->;` return no value
- `->|;` break innermost loop
- `->>;` continue innermost loop

These control forms are statement-level and require statement termination.

## 2.5 Conditional and Iteration Syntax

- Conditional expression/statement uses `cond ? then : else` and block forms.
- Loop syntax uses expression + `@{ ... }` block.

Example:

```vx
(i < 10)@{
  i == 5 ? ->|;
  i = i + 1;
};
```

## 2.6 Optional Semantic Blocks

`#{ ... }` denotes an optional semantic block: parseable code region intended to be ignored when unresolved and non-essential for required semantics.

Parsing keeps the block explicit in AST and defers semantic handling to frontend resolution rules.

## 2.7 Tuple and Multi-Assignment Grammar

- Tuple expression: `(a, b, c)`
- Multi-assignment: `x, y = expr`

The parser distinguishes tuple expression syntax from parenthesized expression by comma presence.

## 2.8 Array Type Syntax

Canonical array type spelling is suffix-only:

- `#T[N]`
- nested arrays: `#u8[H][W][3]`

Array literals use `[ ... ]`; nested shapes use nested literal brackets.

## 2.9 Import and Resource Grammar

- module import statement: `::A::B;`
- resource expression: `::A::B::file.ext` in expression context

Parser disambiguates by context (statement/import vs expression/resource).

## 2.10 Annotation Parse Rule

Annotations only bind when a complete `[[...]]` block is followed by a syntactically valid annotation target in that context.

This avoids accidental capture of ordinary nested arrays such as `[[input(), 2], [3, 4]]`.
