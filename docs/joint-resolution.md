# A joint resolution model

The useful abstraction is a set of admissible complete systems. It is not the
order in which the implementation happens to discover information. Source
behavior, pure calculations, type properties, target obligations and explicit
planning policies restrict that set. Different kinds of questions can require
different properties of the same system.

This document explains the implemented mechanisms and the remaining gaps. The
[acceptance ledger](implementation-work.md) is deliberately broader than what
currently passes. None of the discussion below declares complete RFC support.

## Meaning before strategy

Let `P` be the source, `I` an immutable input snapshot, `T` the target context,
and `A` a set of temporary assumptions. These define a set of admissible models:

```
M(P, I, T, A)
```

A model supplies values for the properties needed by the demanded observation.
It need not assign irrelevant internal implementation details. An unconditional
fact is true of every admissible model in its stated context. A conditional fact
is true only in the models satisfying its assumptions. A proposed choice is
neither kind of proof by itself.

The solver asks about an observation `O`, rather than requiring an arbitrary
complete assignment to every internal variable:

```
answers = { observe(O, m) : m in M(P, I, T, empty) }
```

No answers means contradiction. Exactly one observable answer means successful
closure. The observation includes its required interface, effects and completion
obligations, not just its final number. Two distinct answers establish ambiguity.
A search can remain unresolved because a relevant proof is unavailable, or stop
because its implementation budget is exhausted. Those are distinct outcomes.
A first witness establishes existence, not uniqueness.

This explains why `?{x=?(#u8,1..10);x-x}` can determine zero even though `x`
remains a hidden choice. It also explains why a planner must state its selection
policy: two representations can be physically valid without giving the source
program two different meanings. The MSX experiment states a policy as ordinary
Vexel constraints, including a deterministic tie-breaker.

At a propagation fixpoint, an unfinished result does not prove that its
assumption context is impossible. Structural search retains such alternatives
as unresolved and can continue looking for two distinct completed answers.
Already established finite relations may prove a context contradictory before
its result is known. A satisfying partial assignment establishes only the
consistency of those relations; it does not close the result. See the
[closure contract](closure-contract.md) for the distinction and executable cases.

## Two kinds of rollback

A fixed relation graph supports cheap domain rollback. Each variable has a
finite domain, and each relation lists allowed tuples. Propagation removes
values lacking support. Every removal has a trail entry. Assuming one value
creates more propagation; a contradiction unwinds exactly those removals made
since the decision checkpoint. The relation graph itself remains shared.

Structural decisions are different. An array dimension can create a different
number of elements. A selected record type can change which fields exist. A
repeat condition can create a different number of invocation instances. Undoing
only domain masks would leave invalid graph nodes, cache entries or queued work.

The implementation therefore checkpoints the graph and resolution caches before
expanding an explicit solve region. If a concrete structural property depends
on an unresolved finite choice, it resolves candidate revisions from that
checkpoint. Every revision uses the same parser-owned source sites and the same
semantic rules. The successful observable result is published only after all
alternatives have been checked for agreement. Commit replaces rejected derived
nodes and rebuilds the worklist around stable checkpoint handles.

Whole graph copies are a practical first implementation of this contract.
Persistent arenas, versioned facts and conflict-directed re-expansion could make
it cheaper. They should preserve the same externally tested behavior. A faster
implementation is not allowed to leave the first encountered candidate fixed
just because changing it would be inconvenient.

There is also a distinction between a value and a fresh logical choice. Two
calls of `&pick(){?(#u8,[1,2])}` create two choices, even though their source and
arguments are identical. Reusing one call would silently add an equality that
the program never requested. The implementation therefore disables value-only
call memoization inside finite solve regions, and gives each call site its own
choice identity. Captured places and expression parameters likewise prevent
ordinary pure-call reuse. A future cache must represent freshness explicitly,
for example by replaying a relation template with freshly renamed variables.

Guard context matters too. A result established while `x!=0` holds cannot be
reused as evidence that an unguarded `8/x` is safe. Branch guards qualify relation
generation, and structural assumptions qualify an entire revision. Cache keys
and any future persistent proofs must preserve those qualifications. An input
snapshot is shared immutably across revisions, while the set of dependencies
actually demanded by a revision is isolated.

Decision and tuple-check budgets are shared across the solve, including failed
structural revisions and multiple explicit puzzle regions. Restarting a branch
does not reset permission to spend work. Exhaustion is reported separately from
contradiction. If hidden choices have several assignments but all surviving
models already fix the observable projection, one satisfying witness suffices;
enumerating every hidden assignment would prove nothing additional.

## Late information and source order

Source execution order and resolution scheduling are different. Local stores
have an intentional order: evaluating `x+=1` twice differs from evaluating it
once. The store/load graph represents that order explicitly. It does not depend
on FIFO, LIFO or randomized worklist selection.

Independent type demand separates questions such as the type of an uninitialized
local from the value currently stored there. A record can expose a known field
type while another field remains open. Generic accessors can ask for the field
they use. Final storage or ABI emission can demand more properties later.

Known properties refine within one fixed revision. A different assumption can
produce a different type in a different revision. These statements are
compatible: revising assumptions is not a license to overwrite a proof while
silently keeping its old context. Nor does monotone refinement within a context
require the whole project to accept the first provisional answer.

The current numeric language policy keeps destination types from flowing
backwards through an operator or selecting an overload. That is a specified
conversion policy, not a necessary property of the search architecture. Explicit
finite regions can relate values and structure in both directions through their
constraints. Checked initializer-anchored cycles, independent aggregate
properties, opaque type equalities and transforming field equations now use
that same distinction between provisional assumptions and established answers.
General nonmonotone type-model and captured-schema inference remain open.

## Pure does not mean easy, finite or total

Purity makes evaluation reproducible under a fixed input snapshot. It does not
make every question decidable. Recursive equations can have no solution, many
solutions, or no terminating procedure that the implementation can establish.
Vexel must preserve an unresolved outcome rather than manufacture a runtime
value or call exhaustion an impossibility proof.

The implemented search contract is bounded and explicit: finite scalar choices,
finite aggregates, bounded relation expansion, propagation work, decisions,
structural revisions and recursion depth. A source program can still express a
large or expensive problem. Larger budgets change how much evidence the solver
can obtain; they do not change which completed answer is correct.

A runtime input is a different established property. It is not an uninitialized
compile-time variable that the solver is allowed to guess. The `!@` barrier
likewise preserves a runtime loop even when some bounds happen to be known.

## Calls, aliases and effects

Ordinary function parameters are values. Nested functions additionally carry
lexical bindings to enclosing slots. Calls that can affect captured state thread
that state explicitly and do not enter the cache for value-only calls.
Expression parameters retain a source expression and its caller bindings; each
use creates a fresh invocation of that expression against the current state.
This preserves repeated effects and hygienic name resolution.

Receiver methods use bound locations: a root slot plus already evaluated field
or index obligations. Two receivers can identify overlapping parts of one
aggregate. Their loads therefore observe the same state after a store. No
universal pointer value is exposed to source programs. A non-lvalue receiver
gets private temporary storage.

This is useful for segmented targets. Logical aliasing answers which value is
being updated. Physical access answers how that update can occur on a particular
machine. The target may later choose RAM locations, registers, bank-aware access
sequences or copies without changing the source alias relationship. The current
MSX backend implements resident scalar and internal aggregate storage; arbitrary
mapper-backed logical storage is still work to do.

Value, effect and completion are independent properties. A known
return value does not justify deleting a nonterminating loop or an external
call. A normal continuation reads only paths that can still reach it. Return
payloads retain the state at their exit, so a caller sees captured writes made
before an early return. Functions consume their own returns, loops consume their
own transfers, and enclosing regions receive the remaining exits. Separate type
completion properties allow a return type to be queried without reading its
payload. Unrolled loops, runtime regions and the C backend use this same model.
Exit-bearing loop type queries retain region-completion obligations. Recursive
runtime interfaces are checked through owning-context revisions; supported
loop-completion proofs preserve unchanged locations without executing return
payloads.

Demand must govern graph construction as well as value evaluation. In
`({->7;}) + (missing+=1)`, the right operand has no reachable use. Merely delaying
its arithmetic would be insufficient: eager binding would already reject the
undefined compound-assignment target. A possible exit therefore creates an
evaluation obligation for the continuation. It constructs that expression only
after a normal incoming path is established. The same mechanism covers later
call arguments and aggregate elements, including exits forwarded through
expression parameters. Type demand uses a separate completion property and can
construct the type obligations without executing return payloads.

## Compiling a question that the solver keeps asking

The native relation filter is a concrete instance of compilation inside
resolution. The source rules first establish allowed tuples using Vexel numeric
semantics. The generated C++ function repeatedly checks support against current
domain masks. It cannot select a source interpretation or change a constraint.
The ordinary solver still owns search and closure.

The interface makes interchangeability testable: native and ordinary filtering
receive the same masks and budget, and must return the same supported values,
status and work count. Verification mode compares every invocation. Failed
compilation falls back to the ordinary implementation.

The next generalization is a family of pure evaluators with explicit interfaces:
input domains, result properties, numeric rules, relevant target facts and input
revision. A cost policy can decide when compiling one is worthwhile. Caching
must include those conditions and the toolchain realization. A cached result
from an older assumption context is a candidate for reuse, not an unconditional
fact. The current implementation compiles relation filters only and always uses
fresh build directories; it does not yet implement this general evaluator cache.

## Target facts and measured costs

The NEO-8 target contributes facts before source resolution, including memory
and mapper parameters. Exported projects retain C, assembly override points,
Makefiles, linker maps and machine-execution reports for inspection.

The frame-policy experiment now closes a feedback loop. It builds automatic and
static-frame candidates, validates the workload against the Vexel resolver,
collects actual linker placement and Z80-model observations, and supplies those
measurements to a Vexel policy puzzle. A changed policy input can select a
different project. Modified measurement files invalidate the recorded evidence.

A measured stack high-water mark is a fact about one execution, not a proof about
all executions. Likewise, the static-frame non-overlap contract is established
by the owned experiment's caller/interrupt arrangement; it cannot be inferred
for arbitrary applications merely because one workload happened to pass.
General reentrancy analysis, memory-mapper allocation, code partition search and
worst-case stack proofs remain separate obligations.

## Advice from a model

An external provider may recommend a currently admissible variable and value.
It can use an LLM, a hand-written heuristic, or a previous transcript. The core
validates the request identity and the current domain, then changes exploration
order while retaining every alternative. Model text never becomes a proof,
constraint, source fragment or shell command.

This makes an unreliable proposal useful without making it authoritative. Poor
advice may cost time. A timeout, stale answer or invalid value falls back to
ordinary search. Recorded requests and responses permit offline replay for the
same context. The current interface guides finite domain and structural search;
it does not propose new algorithms or arbitrary source rewrites.

Allowing transformations would require another interface: a proposed
transformation, its preconditions, a validation method and a cost estimate.
There is no reason that every such validation must be a complete equivalence
proof, but the implementation must say what was checked. Passing a handful of
examples cannot silently become universal semantic equivalence.

## What remains open

The implemented pieces establish domain backtracking, structural revisions,
independent type demand, scoped rollback, optional native execution, checked
advice and a bounded target-feedback experiment. They still need broader
integration at their closure boundaries. Runtime recursion, fixed point and
IEEE source arithmetic, representation casts, lexical modules, persistent
bindings and aggregate target ABIs are now implemented and tested. The remaining
ledger concerns general cyclic schema inference, target storage and physical
planning, stack proofs, and broader checked proposals. These require explicit
acceptance evidence; they are not implied by the presence of a resolution graph.
