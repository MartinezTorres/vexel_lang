# Anti-goals — What Vexel Refuses

**Role**: Anti-goals (explicit refusals and scope boundaries).

Vexel is not neutral about language design.
It rejects features that block whole-program reasoning.

## Opening Refusal

Vexel is not a language that optimizes for unrestricted local freedom.
It is a language that optimizes for global compiler visibility.

## Core Rejections

## 1) Raw pointers and pointer arithmetic

Rejected because unrestricted pointer graphs erase compile-time certainty.
Without certainty, CTE and DCE collapse into guesses.

## 2) Split toolchains and generator culture

Rejected because spreading semantics across side tools fragments ownership.
Vexel keeps compile-time behavior inside the language and compiler pipeline.

## 3) Implicit runtime magic

Rejected because hidden behavior cannot be proven by frontend analysis.
Semantics must remain explicit and testable at the language boundary.

## 4) Comfort defaults that hide tradeoffs

Rejected because this project is intentionally opinionated.
The trade must remain visible: fewer escape hatches, stronger reasoning.

## 5) Generic template-site aesthetics as identity

Rejected because the project must be memorable as a doctrine and machine,
not as a generic product page.

## What Vexel Does Not Promise

- Maximum convenience for every coding style.
- Automatic compatibility with unconstrained low-level idioms.
- Friendly abstraction over unknown runtime behavior.

## Filter Statement

If you need unconstrained pointer-level mutation first and whole-program reasoning later,
Vexel is likely not the correct tool.

## Related Documents

- [Landing Page](index.html) — thesis and route selector.
- [RFC](vexel-rfc.md) — normative law.
- [Specification](spec/index.md) — detailed semantics.
- [Architecture](architecture.md) — ownership and pipeline.
- [Tutorial](tutorial.md) — practical route through examples.
