# A relational core for types, activations and realizations

This is the supported relational implementation, built as `build/vexel`.
The directory name is historical. It does not depend on the
current solver's `Type` class, alias proofs, property-equation loops or graph
node representation. Its acceptance criteria are observable answers and
admissible realizations, not compatibility with those mechanisms.

The [source adapter](SOURCE.md) reuses Vexel's parser and contributes source
relationships directly to this core. It generates C without linking the earlier
solver. The C++ API demonstration remains available as `vexel-relational`.
The former semantic-transaction and recursive capture-proposal mechanisms were
removed in the [language revision](../../docs/language-simplification.md).

From the repository root:

```sh
make relational
make test-relational
build/relational/vexel-relational --mode serial --budget 6
build/relational/vexel-relational --mode overlap --budget 6
build/relational/vexel-relational --mode retained --budget 6
make relational-msx
make relational-source-msx
make relational-control-msx
make relational-execution-msx
make sanitize -C experimental/relational
```

For logged sanitizer runs, use the optional Make variable instead of shell
redirection. The log's parent directory must already exist; an absolute path
also works when running from an isolated snapshot:

```sh
make sanitize -C experimental/relational SANITIZE_LOG=/tmp/vexel-relational-sanitize.log
```

The root Makefile accepts the same variable. Both forms preserve the full
address, undefined-behavior and leak checks and propagate test failures.
In an agent environment with an existing `make sanitize` approval, keep that
literal command prefix: shell redirection or a shell wrapper can require a
separate approval. Leak checking still needs an environment that permits its
process inspection; disabling it is not the logging solution.

`relational-msx` requires the same SDCC toolchain as the existing backend. It
creates a fresh directory under `build/relational-msx`, retaining editable
projects, host checks, placement reports and Z80 execution evidence.

## What is different

Types are terms with variables, not an enum whose unknown case must eventually
be replaced by a widening candidate. Examples of terms contributed by adapters
are `UInt(16)`, `Array(element,length)`, `Record(identity,fields,captures)` and
`Function(argument,result)`. The kernel treats their constructors uniformly.

Unification relates their structure in either direction. Repeated variables
express equality; nominal identities and captured values are explicit terms.
An occurs check rejects infinite term structures. Each invocation of a generic
relation receives fresh variables, so independent applications can specialize
without sharing their type variables accidentally.

Relations consist of alternatives, each with a head pattern and further
obligations. Choosing an alternative instantiates and checks those obligations
in a private context. An alternative can introduce more structure or more
questions; it need not fit a precomputed flat table of numeric tuples.

Shared obligation bundles retain property identities across cycles and are
activated separately in each search context. Delayed builders add obligations
once declared inputs are ground; they receive no mutable solver context.
This lets source overloads and branch conditions determine which relationships
exist without making a consumer's desired result a selection premise. Semantic
ambiguity is explicit even when tied source candidates have equal results.
Builders are trusted adapters that construct obligations, not unverified
external advice providers. Structural and primitive propagation runs before
search explores clause alternatives. Fresh clause guards also reject already
contradictory candidates before they become search decisions; their bindings
remain private.

Candidate witnesses attach implementation artifacts to the context whose
obligations justify them. Witness terms must be ground before that context can
publish a model; duplicate names must unify. Rejected alternatives and previous
revisions cannot contribute artifacts to the selected model. The source adapter
uses this operation for runtime function definitions and completion traces.
This is separate from source observations and optimization objectives: a
witness does not give a backend permission to choose source meaning.
For proof requests whose question concerns an artifact itself,
`Request::observedWitnesses` adds the named artifacts' presence and values to
the observation. These entries are collected only after all candidate
obligations close. A complete candidate can prove absence; an unfinished
candidate keeps the overall request unresolved. Different completed presence
or value results are ambiguous even when the ordinary observation agrees.

Integer constraints and structural terms use the same context. Addition and
multiplication propagate known values in either direction. Fraction-free linear
elimination additionally solves coupled integer equations. Nonintegral uniquely
determined answers are rejected, and source-independent numeric domains survive
algebraic simplification and variable aliasing. For example, simplifying
`0*x=0` still requires `x` to be an integer; a later record constraint cannot
silently satisfy it.

This closes dimensions such as:

```
a = b + 1
a + b = 7
first  = Array(UInt(8), a)
second = Array(UInt(16), b)
```

The result is lengths 4 and 3 and a compact combined extent of 10 bytes. Neither
dimension has an initializer seed or an enumerated domain.

The type tests also define a relation that maps `UInt(8)` to `UInt(16)` and
vice versa, recursively through arrays. Mutually applying that relation gives
two scalar type alternatives; requiring one owner's compact size to be one byte
selects the appropriate solution. Without the extra requirement, observing the
types reports ambiguity. This is relational search, with no monotone widening
policy smuggled into the kernel.

A query can expose a known record field while unused fields and schema captures
remain variables. Requesting the entire record then remains unresolved until
those properties are supplied. The adapters must represent the language's actual
schema requirements; an unconstrained variable alone does not encode arbitrary
missing validation work.

## Closure and choices

Relations explicitly say whether their definitions are closed. No matching
alternative in a closed relation is a contradiction. An open or unknown
relation retains an unfinished alternative. Finding one completed answer cannot
erase that alternative. Two differing completed observations still establish
ambiguity. Work and decision limits report exhaustion.

The request distinguishes a source observation from a physical realization.
Several realizations may implement the same observation. An explicit
lexicographic integer policy selects among the checked realizations. It cannot
choose between disagreeing source observations.

A returned provisional witness is not permission to emit a plan. The CLI only
exports a plan after the outcome is resolved. Contradiction is relative to the
supplied relation model and its declared candidate families, not a proof that
every imaginable implementation of a Vexel program is impossible.

## Activation contracts supply lifetime constraints

The activation adapter accepts a finite event graph, causal ordering edges and
storage lifetimes. It contains no thread or interrupt categories. Ordinary
nested activations and output retained after a producer returns are represented
by their start/completion relationships.

For a closed event graph, all orders allowed by its edges are admitted. If one
lifetime ends before another begins, or they share an explicit completion/start
event, storage reuse is possible. Lifetimes are half-open intervals. Otherwise an
admitted overlap requires distinct mutable byte intervals. An open contract
keeps unknown relationships unresolved. Established nesting can already prove
overlap in an open contract; a conservative guess of overlap cannot refute a
potentially valid shared plan. Contradictory causal edges are rejected.

The adapter contributes ordinary storage constraints to the same search as type
and dimension relations. Types are not fixed before placement is attempted.

## The executable six-byte experiment

Two activations each use a three-element payload. Boundary inputs are 0..250;
payload elements are therefore 0..252 and may use 8- or 16-bit unsigned storage.
The profile explicitly prefers 16-bit storage and then minimizes occupied
extent. That preference is an illustrative policy, not a measured speed claim.

| Boundary contract | Selected buffer type | Offsets | Occupied extent |
|---|---|---|---:|
| A finishes before B begins | `Array(UInt(16),3)` | 0, 0 | 6 bytes |
| B activates while A remains live | `Array(UInt(8),3)` | 0, 3 | 6 bytes |
| A returns, but its sink retains the payload across B | `Array(UInt(8),3)` | 0, 3 | 6 bytes |

For overlapping activations, the preferred wider representation cannot fit.
The machine backtracks to a narrower element type and separate buffers. The
retained-output case demonstrates why returning from a source is not by itself
proof that its storage is dead.

The original `examples/relational_msx.py` bootstrap used the earlier frontend;
it is historical material. `make relational-msx` now uses the direct source
adapter via `examples/relational_source_msx.py`. The six-byte budget covers
payload bytes only: compiler temporaries, resident code, globals and stack
remain separate. The exported project checks linked MSX placement.

## What this establishes, and what it leaves open

The experiment establishes a common mechanism for structural type alternatives,
generic instantiation, some value/shape equations and activation-constrained
physical plans. Its independent oracles cover 5,952 linear systems, 1,296
completed/unfinished outcome combinations and 216 type/layout configurations
across worklist and branch orders. The core and CLI tests also run under address,
undefined-behavior and leak sanitizers.

The implementation does not close the entire experimental roadmap. In particular:

- The [source adapter](SOURCE.md) covers structural overload matching, aggregate
  execution, recursive captures, finite solving, deferred expressions and
  persistent/external effects. Cyclic field-type joins, runtime schema invariants,
  the numeric/operator families and scoped resources also have executable
  adapters. Signature capabilities replace semantic transactions, and recursive
  state requires a grounded interface. Remaining interactions are tracked in the
  [completion ledger](../../docs/relational-completion.md).
- The source adapter derives returned-output storage from source structures;
  boundary ordering is supplied explicitly. It does not infer lifetime
  contracts from existing Vexel call graphs. Finite occurrence graphs coexist
  with repeated stream adapters using explicit bounded pools and checked leases.
  Arbitrary concurrency and unbounded ownership are not inferred.
- The demonstrated representation family and offset domains are finite and
  explicit. Nonlinear integer solving and underdetermined integer systems can
  remain unresolved. Infinite recursive rule expansion can exhaust its budget.
- Candidate contexts are copied at search decisions; speculative clause matching
  uses reversible transactions. The source workspace retains closed whole-request
  results across live edits with checked support. Reuse within a request,
  on-disk cache persistence, tabling, learned conflicts and advisor integration
  remain future work.
- The compact layout relation defines byte encodings, not native C struct
  layout. Owned mapper storage uses explicit segment grants. Assembly analysis
  can bound source entries under declared environment/external-call assumptions;
  it does not prove arbitrary embedding code or allocate operating-system RAM.

This core is now the supported implementation. Source adapters and specialized
proof helpers contribute relationships and justified summaries; they must not
publish a private candidate as a source fact. Further language work should
preserve that boundary without preserving obsolete implementation details.
