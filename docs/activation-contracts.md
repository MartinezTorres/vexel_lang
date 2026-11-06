# Activation contracts at graph boundaries

This describes the replacement for the older backend-specific reentrancy
policy; finite occurrences and bounded repeated streams are implemented by the
supported relational frontend. Concurrency enters through the
graph's sources, sinks and their activation contracts. Interrupts, threads and
platform callbacks are mechanisms of a boundary adapter, not language concepts
that the source resolver must reconstruct.

A [relational implementation](../experimental/relational/README.md)
implements explicit event/lifetime contracts together with structural type and
placement alternatives. Its generated host-C and MSX examples exercise serial,
overlapping and retained-output lifetimes. A
[source-to-execution slice](../experimental/relational/SOURCE.md) additionally
derives result storage from actual Vexel source and combines it with explicit
boundary ordering. This is the root `build/vexel` executable. General inference
of external event lifetimes remains open; internal call/activation facts support
conditional frame sharing. The earlier frontend's `--msx-exclusive` and blanket
external-effect rejection are historical, not this contract.

The relational implementation now separates named external resource footprints
from a supplied no-feedback promise. It accepts `--resource-read`,
`--resource-write`, `--sink-no-feedback` and `--access-order` facts, retaining
output lifetimes independently of execution order. Its per-function storage
policy can use a non-activating sink without treating that sink as pure.
Finite named occurrences now extend that implementation. `--occurrence A:f`
and `--occurrence B:f` distinguish two activations of the same source, each
with an independent result owner. `--mode graph` and `--event-order` describe
begin, produce and release events. Unordered lifetimes admit overlap; a proved
release-before-begin edge permits result storage reuse. Finite graphs support
retained results and a callback that activates a separate occurrence while its
caller is suspended. The exported C entry names denote these occurrences.

Each name owns at most one activation and outstanding result in one finite
epoch. The adapter must honor the event graph and release the epoch before
reusing its names. C return completes execution; it does not release the result.
This finite graph does not discover callback occurrences automatically.

For repeated events, `[[stream(capacity)]]` selects a different bounded adapter:
a shared execution gate, per-source result pools, checked ownership leases and
explicit release. Successful submission retains one slot until release; a full
pool or nested submission returns backpressure without running the source.
Tokens never wrap and cannot become another owner's result. This permits
overlapping result lifetimes with serialized source execution. Concurrent
boundary adapters must serialize pool API access. Automatic inference of event
bounds and arbitrary overlapping execution remain outside this adapter.
See the [backend contract](backend-msx-neo8.md) for the lease ABI and the
[completion ledger](relational-completion.md) for verification
and the [source adapter](../experimental/relational/SOURCE.md) for the exact flags.

## Occurrences have lifetimes

A source supplies an observation or event. A sink consumes an observation or
issues an external request. Their boundary contract describes which occurrences
can happen, their causal relationships, and which work may remain outstanding.
An external operation may expose both input and output ports; its C signature
alone does not establish its activation behavior.

An activation is one occurrence of work in the graph, with particular inputs,
state relationships and completion obligations. A function declaration or graph
node is a reusable description; it is not the identity of every runtime
activation of that description.

An activation's storage remains live until its last required use. Emitting a
result does not necessarily retire the activation. For example, an output
adapter may still be consuming a buffer. The contract must establish a copy,
transfer of ownership, or completion event before that storage can be reused.
This applies even though Vexel exposes no universal pointer.

## Describe permitted overlap directly

The boundary contract supplies facts such as:

| Relationship | Meaning |
|---|---|
| Successive occurrences of source A are serial | A new occurrence cannot overlap the preceding live activation covered by that contract |
| A and B are exclusive | Their covered activations cannot overlap |
| A and B may overlap | A realization must tolerate their permitted overlap |
| At most k occurrences are outstanding | Storage can be planned against that bound when its scope covers the required lifetimes |
| A sink may activate source B before its caller retires | The resulting nested activation can overlap its caller |
| A completion precedes a new activation | That causal edge can establish a storage-reuse opportunity |

The scope of a bound matters: one outstanding external request does not imply
one live invocation of every helper it reaches. Pairwise overlap permissions
also do not by themselves establish a bound on the number of outstanding
activations.

Self-overlap and overlap between different sources are separate questions.
Two individually serial sources can overlap each other. Pairwise exclusion is
not transitive: excluding A/B and B/C does not exclude A/C. An unspecified
relationship is unknown, not proof of exclusion.

These are statements about admissible boundary behavior, not requests for a
particular operating-system scheduler. The backend adapter must implement or
honor them. It cannot silently queue, drop or delay source occurrences merely
to make a static allocation work; any such policy must satisfy the boundary's
ordering, delivery and completion contract.

The resolver's FIFO/LIFO/random worklist is a different mechanism: it orders
attempts to establish facts. Changing that order must not change the permitted
runtime activations or their observable relationships.

Internal causal structure contributes further facts. A caller and an ordinary
callee are both active while the call is outstanding. Recursive calls can
create overlapping helper activations even when the external source is serial.
More precise liveness can establish that particular caller temporaries are dead
across a call, without declaring the entire caller activation finished.

An external effect alone establishes neither overlap nor exclusion. A sink
that cannot activate a source can coexist with static temporaries. A sink with
a permitted feedback path may expose the current activation to reentry. The
feedback relationship belongs in the graph boundary contract.

## Derive storage interference

For two storage obligations x and y, the useful question is:

```
Can an admissible execution require x and y to remain live simultaneously?
```

If the answer is yes, their distinct mutable contents cannot occupy the same
bytes. A proof that the answer is no permits reuse. If the answer is unknown,
the solver cannot use exclusion as a premise. Immutable equal contents can have
their own sharing proof; they do not require the same interference rule as
independent mutable cells.

This replaces a single global `reentrant(function)` switch with obligations
about particular uses and candidate realizations. A helper reached from two
overlapping sources could use separate storage per activation, bounded frame
slots, or separate materializations for the two source contexts. Each proposal
must close the same source and boundary obligations.

Automatic stack storage is one implementation of suitably nested lifetimes.
It is not an automatic solution for arbitrary outstanding asynchronous work.
A finite physical memory plan still needs the relevant overlap and extent
bounds, or a defined resource-exhaustion behavior. Measuring one execution does
not establish those bounds.

Mapped windows and other target resources can participate in this reasoning.
Their use intervals, restoration obligations and interference are physical
constraints of a candidate. The source graph need not acquire thread or
interrupt syntax to express that two resource uses must not overlap.

## Pure values and state relationships stay explicit

For fixed source observations and explicit state inputs, pure graph relations
determine their outputs. The realization may schedule independent work in any
way allowed by the boundary contract.

If two activations interact through persistent logical state, their ordering,
disjoint ownership or allowed combination must also be represented. Merely
declaring both activations permissible does not explain which version of state
each reads. The backend cannot settle an observable ordering ambiguity by
choosing a convenient memory allocation or evaluation order.

Local mutation can continue to use the existing ordered state edges. Cross-
activation state needs corresponding boundary relationships. No implicit shared
mutable communication is introduced by placing two values in the same physical
address space.

## The same candidate question reaches the backend

A realization candidate proposes storage, materialization, placement and access
choices. It must satisfy:

1. The demanded value, interface, state, effect and completion relationships.
2. The source/sink activation and delivery contracts.
3. The chosen target's resource and access constraints.

Activation facts need not be fully settled before any physical planning begins.
A proposed reuse may demand an exclusion proof; failure of that proposal may
lead to separate frames or a different materialization. Later information may
revise the candidate in its owning context. An LLM can propose those choices,
but cannot invent an exclusion fact or silently narrow the allowed boundary
behavior.

These are architectural obligations. The current collection of specialized
proof mechanisms is not the only permitted implementation of them.

## Tests must state which promise they protect

The existing executable is useful as a reference for established behavior. Its
internal representation is not the language specification.

Independent arithmetic and finite-model oracles, source-order permutations,
effect observations, search rollback checks and target execution remain useful
acceptance evidence. When a language rule changes intentionally, the relevant
expected observations must change with an explicit explanation of the rule.

Tests requiring particular `type_aliases`/`type_equations`/`type_properties`
arrays, proof-node counts, or one exact generated-C storage spelling can be
replaced when the implementation changes. A test of a temporary unsupported
case can become a success test when the new mechanism discharges its obligation.
Removing an implementation-specific assertion must not erase the underlying
question about correctness.

A historical example is `test_target_facts_and_static_frame_contract` in
`tests/test_msx.py`, whose earlier exporter rejected static frames whenever a
call was external. The current frontend makes that decision contract-dependent:
a non-activating sink may be admissible, while unknown or overlapping feedback
requires a suitable realization. The current contract/frame suites check that
distinction instead of retaining the old blanket rejection.

Machine-level interrupt tests still check the boundary adapter's implementation
of its declared contract. They need not introduce interrupt semantics into the
pure language or constrain a new solver's graph representation.

## Acceptance cases for the architectural experiment

- A serial source reaches a sink with no activation feedback. Static storage
  can be proposed even though the output is externally observable.
- Two individually serial sources may overlap and reach one helper. A shared
  mutable frame is rejected unless a more precise lifetime proof allows reuse;
  separate frames or context-specific materializations can be considered.
- A serial source recursively reaches the same helper. The external seriality
  assertion does not collapse overlapping recursive storage.
- A sink can activate a source before completion. The new occurrence sees its
  own activation storage while explicitly ordered state relationships persist.
- A sink retains access to an output after the producer returns. Reuse waits
  for the declared completion or uses an independently owned copy.
- An overlap relationship is unknown. A candidate cannot assume exclusion to
  satisfy a memory budget, and search exhaustion cannot prove that candidate
  impossible.
- Replacing an exclusive relationship with permitted overlap invalidates the
  affected reuse proof and physical plan while preserving unrelated source facts.

These cases define the experiment independently of the current graph classes,
proof-array formats, allocation policy or solver scheduling strategy.
