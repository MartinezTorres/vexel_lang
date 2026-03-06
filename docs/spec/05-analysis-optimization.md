# 5. Analysis and Optimization Model

RFC references: [Preamble](../vexel-rfc.md#preamble), [Type Inference & Generics](../vexel-rfc.md#type-inference--generics), [Runtime Semantics](../vexel-rfc.md#runtime-semantics)

Operational contract references: `frontend/src/architecture.md`

## 5.1 Ownership

Frontend owns semantic analysis and optimization decisions:

- compile-time facts,
- residualization,
- reachability and usage,
- mutability and reentrancy analysis,
- final live-set pruning.

Backends consume analyzed output.

## 5.2 Pass Intent

Conceptual pipeline stages:

1. resolve names/symbols
2. type-check + inference
3. lower typed AST
4. collect compile-time facts
5. residualize from facts
6. analyze graph properties
7. validate concrete value-use typing
8. prune to live declarations

## 5.3 CTE Fixpoint Behavior

Fact solving is iterative and dependency-driven.

Key invariants:

- Unknown may become Known/Error.
- Known must not revert to Unknown.
- dependency loops are detected and diagnosed deterministically.

## 5.4 Sub-Expression Granularity

CTE and DCE are not only symbol-level.

Required behavior:

- expression-tree nodes can be independently known/unknown,
- dead branches are pruned when conditions are known,
- statement-level residual code contains only reachable/effective work.

## 5.5 Reachability Roots

Semantic roots are ABI-visible boundaries and required runtime entry surfaces.

Pure internal functions are retained only if reachable through live graph paths.

## 5.6 Mutability Analysis

Mutability is inferred from graph behavior, not user hints.

A declaration assigned along any live runtime path is mutable.

Compile-time-known immutable values are candidates for full folding.

## 5.7 Reentrancy Analysis

Reentrancy facts are graph-derived and propagated.

Frontend computes facts; backends decide target-level implementation strategy for those facts.

## 5.8 Generic Instantiation Interaction

Analysis tracks per-instantiation behavior.

Different call contexts may produce distinct instantiated variants and distinct constexpr facts.

## 5.9 Type-Use Validation Boundary

Concrete type requirements apply to reachable value uses.

Unreachable or value-unused unresolved return chains are not promoted to hard errors unless they become semantically used.

## 5.10 Frontend Completeness Requirement

Backends must not be used as fallback semantic engines for missing frontend analysis.

If frontend lacks enough information for a required semantic guarantee, compilation must fail clearly.
