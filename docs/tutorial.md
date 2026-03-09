# Tutorial — Learn the Shape of Vexel

**Role**: Tutorial (learn how Vexel feels in practice).

This tutorial is a guided route through runnable examples under `examples/tutorial/`.
The fastest path is to open each step in the playground and inspect emitted output files.
Web route page: `docs/tutorial.html`.

## How to Use This Tutorial

1. Open a step in `docs/playground.html`.
2. Compile with backend `c` first.
3. Read the generated output files and build log.
4. Move to the next step.

The tutorial is designed to teach shape and tradeoffs, not only syntax.

## Guided Steps

1. [Quick Add](playground.html?file=examples/tutorial/quickadd.vx&backend=c)
2. [Hello + extern](playground.html?file=examples/tutorial/hello.vx&backend=c)
3. [Structs and methods](playground.html?file=examples/tutorial/structs.vx&backend=c)
4. [Arrays and shapes](playground.html?file=examples/tutorial/vector_matrix.vx&backend=c)
5. [std::math arrays](playground.html?file=examples/tutorial/math_arrays.vx&backend=c)
6. [std::bits reinterpret](playground.html?file=examples/tutorial/bits_reinterpret.vx&backend=c)
7. [Constexpr + DCE](playground.html?file=examples/tutorial/constexpr_dce.vx&backend=c)
8. [Prime sieve](playground.html?file=examples/tutorial/sieve.vx&backend=c)
9. [Multi-file module](playground.html?file=examples/tutorial/multifile/main.vx&backend=c)

## Step Intent (Quick Reference)

- Quick Add: baseline shape and compile-time fold.
- Hello + extern: ABI import and side-effect boundaries.
- Structs + methods: type constructor + method/operator surface.
- Arrays + shapes: nested arrays and per-element operators.
- std::math arrays: bundled math lift behavior.
- std::bits reinterpret: explicit bit reinterpretation contract.
- Constexpr + DCE: dead symbol removal and residual minimization.
- Prime sieve: loop-heavy compile-time execution behavior.
- Multi-file module: import resolution and symbol flow.

## What to Observe

- Compile-time execution folding paths into constants.
- Dead code elimination before backend emission.
- Clear ABI boundaries (`&^`, `&!`, `^`, `!`, `!!`).
- Backend output differences for the same analyzed program.

## Related Documents

- [Landing Page](index.html) — thesis and route selector.
- [RFC](vexel-rfc.md) — normative law.
- [Specification](spec/index.md) — detailed semantics.
- [Architecture](architecture.md) — frontend/backend machine view.
- [Anti-goals](anti-goals.md) — what Vexel refuses.
