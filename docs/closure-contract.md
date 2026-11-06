# What it means for Vexel to finish

Vexel resolves questions about a program in a specified context. Executing a
pure expression, finding a type, solving a finite puzzle, choosing a physical
implementation and compiling an accelerator are ways to answer such questions.
They do not need separate definitions of what the source means.

The context contains the source declarations, observed input snapshots, target
inputs, requested observations and explicit contracts. A speculative decision
extends that context with an assumption. Answers derived under that assumption
belong to that context; they are not automatically facts of the original one.

This is the organizing contract for the implementation. The RFC defines source
notation and arithmetic; the executable profile records the supported rules;
the acceptance ledger records tests and implementation gaps. A closed model of
resolution does not imply a terminating decision procedure for every pure
recursive program.

## Five outcomes, with different evidence

| Outcome | What has been established | What may follow |
|---|---|---|
| Resolved | The demanded observation is determined, including its required interface, effects and completion obligations | Publish the result or a residual implementation with its runtime contract |
| Contradictory | Established constraints exclude every admissible alternative in the relevant closed domain | Reject that context |
| Ambiguous | Two admissible completed alternatives disagree on the demanded observation | Report both possibilities; a heuristic cannot choose the source meaning |
| Unresolved | A relevant obligation lacks a proof or required information | Keep it pending or report incomplete resolution |
| Exhausted | An implementation budget stopped the search or proof | Report the budget and unfinished work |

A malformed source construct is a language error, independently of whether a
particular finite puzzle is satisfiable. The search report therefore also
distinguishes ordinary rejection and an invalid replay transcript.

A runtime input is already known to be a parameter of residual behavior. Closing
that behavior does not require every runtime argument to produce the same
number. An unresolved compile-time question is different: it cannot become a
runtime input merely because propagation stopped.

An unfinished branch is not a contradiction. This matters even for a very small
example:

```vexel
a=b;
b=a;
^answer:#u8=?{
    n=?(#u8,[0,1]);
    size:#u8[n+1];
    (n!=0)?a:(#u8)7
};
```

The zero branch supplies 7. The other branch has an unproductive dependency
cycle. It has not been proved impossible, so the resolver cannot report a
unique answer of 7. The array dimension merely makes structural branching
visible; adding or removing that unrelated dimension must not change the
distinction between an unfinished proof and a rejected candidate.

Conversely, a constraint can make the unfinished path irrelevant:

```vexel
a=b;
b=a;
^answer:#u8=?{
    n=?(#b,[0,1]);
    ?(n==0);
    n?a:(#u8)7
};
```

Already established relations can refute the nonzero context before its result
is known. A satisfying assignment to a partial relation set cannot establish the
whole result. At a propagation fixpoint, remaining finite decisions may need
structural exploration. If two completed alternatives already disagree, that
establishes ambiguity even when a third alternative remains unfinished.

These distinctions apply through nested search, optional blocks and type proofs.
An optional block recognizes specified forms of absence; it cannot use an
unfinished proof or exhausted budget as a reason to take its fallback.

## Closure belongs to a property

The type of a field is a smaller question than the type of its whole record.
Array length, element interface, captured schema identity, values, effects and
normal completion can have different dependencies. A proof should answer the
question that was asked without silently discharging the others.

For example, the solver can establish the common width of the `x` fields here
without reading `unknown`:

```vexel
^answer:#u16={
    unknown:#u8;
    #Box(x,data:#u8[x+unknown]);
    #Pair(a:#[#Box(b.x+1,b.data)],b:#[#Box(a.x+1,a.data)]);
    v:#[#Pair(#Box((#u8)2,[1,2,3]),
              #Box((#u16)2,[1,2,3])).a.x]=300;
    v
};
```

The answer is 300. Demanding the whole record later still requires its captured
schema and reports the uninitialized read. The earlier field proof remains a
field proof; it never represented an empty or automatically satisfied schema.

There are three distinct implemented kinds of type evidence:

1. An opaque-variable proof can establish a type equality through an ordinary
   helper without supplying the variable's representation. A closed equality
   component joins all its initializer witnesses property by property.
2. A candidate equation proposes an interface from initializer information,
   evaluates the original query under a shared vector, and checks the resulting
   interface exactly before publication.
3. A property equation applies that candidate procedure to a field or element
   path while retaining the containing structure and schema obligations.

Every candidate revision uses fresh derived state. Overload selection can reveal
another property equation, whose initializer witness must join the same proof.
A witness that depends on the candidate is rechecked after revision. Temporary
errors may disappear when another equation changes the vector. Value demands
inside a trial use that trial's interface consistently.

The common-type iteration is an explicit supported inference rule. It is not an
exhaustive search over arbitrary nonmonotone type or schema models. Initializer
witnesses do not invent missing values, and destination types do not change an
operand's wrapping arithmetic. Explicit finite choices provide a different,
relational search mechanism. Keeping these policies stated is essential: an
implementation strategy must not accidentally become an unstated typing rule.

## Revisions preserve meaning, not every intermediate fact

Within a fixed context, established properties refine consistently. Revising an
assumption restores the checkpoint that owns its consequences, including graph
expansions, subscriptions, invocation caches, interface checks and generated
candidate information. Changing an observed input creates a new input context.

The implementation uses graph copies for structural revisions and trails for
fixed finite domains. These are engineering choices. Persistent graph storage,
learned conflicts or more selective invalidation may replace them while keeping
the same contract.

The complete relevant declaration set is part of the premises. Discovering one
applicable overload does not establish that no better candidate exists. A probe
needs evidence for absence. A declaration that is unused now may be demanded
later. Local stores still have source evaluation order; worklist order is a
separate implementation detail.

Input observations must also survive the right boundaries. A rejected optional
block may have read a resource whose contents determined its failure. That
resource is a premise of the successful fallback. Discarding speculative values
does not erase the observation that justified rejecting the alternative.

The build projection records observed files and relevant directory changes.
An absent preferred import depends on an existing parent directory, so creating
the module can trigger a new solve. The same applies when a project module can
override a relative or bundled fallback. Empty Make prerequisite rules allow
deletion to rerun resolution instead of preventing Make from starting it.
Input snapshots are shared across speculative revisions. Dependency sets follow
proof ownership: an optional fallback retains the inputs that justified absence,
while inputs used only by a discarded type candidate stay outside the accepted
context. Derived values, stores and graph expansions remain isolated.

This uses Make's timestamp model for rebuilds and a fresh graph for each tool
invocation. Input paths are expected to remain stable during that invocation;
lazy byte/listing snapshots are not an atomic snapshot of the filesystem.
Keeping generated outputs in a separate build directory also avoids unrelated
output creation invalidating a watched source directory. This is not a persistent,
content-addressed incremental solver.
Process imports remain explicit opt-in input operations: their undiscovered
external dependencies need user-supplied build prerequisites.

## A target supplies constraints and implementations

Vexel values and logical locations do not imply one universal address space.
The source specifies copies, identity of receiver locations, reads, writes and
effects. A target realizes those operations with registers, resident storage,
mapped windows, calls or other access protocols.

The NEO-8 implementation installs selected machine facts before source
resolution. It then emits an editable SDCC/megalinker project. Its current
profile uses 16 KiB of page-3 RAM and explicitly guarantees zero additional RAM
mapper segments. ROM bank selection belongs to the physical realization.
Automatic allocation of mapper RAM would require a new access realization and
ownership contract; a numeric segment count alone cannot make ordinary C
addresses safe across mapping changes.

Three claims must stay separate:

- The source result and required residual effects are established by source rules.
- A chosen project fits its measured linked ROM and resident memory limits.
- A particular execution used an observed amount of stack and produced the
  tested results under a stated machine model and interrupt workload.

The third claim is not a worst-case stack proof. Likewise, a static frame needs
a non-overlap contract; passing one workload cannot establish that no callback
or interrupt can reenter it. Current `--msx-exclusive` use is an explicit caller
contract, and recursive activations retain automatic storage.

The existing experiment closes a bounded feedback loop: produce candidate
projects, build them, validate the workload, collect measurements, then solve a
Vexel policy puzzle over those measurements. The policy can choose a different
candidate when the budget changes. Measurements remain bound to their ROM,
project and workload; they do not become unconditional properties of all
realizations of the source.

Handwritten C or assembly stays inspectable and replaceable. Editing it changes
the implementation whose evidence must be checked. Placement checks and a set
of execution tests establish exactly those claims, not universal equivalence to
the original source.

## Compilation and advice are strategies inside the contract

The native relation filter is already an example of compiling a question during
resolution. Ordinary Vexel rules establish the allowed tuples. Generated C++
repeatedly computes support against domain masks. Optional differential checking
compares every call with ordinary propagation, including work accounting and
exhaustion. Failed compilation leaves the ordinary implementation available.

A more general compiled evaluator would need the same explicit interface:
input properties, arithmetic, traps, effects, context and supported result
claims. Compilation does not authorize executing an external runtime effect on
the host or replacing arbitrary-width arithmetic with host-width arithmetic.

An LLM provider can recommend existing numeric or structural decisions. The
resolver validates identities and domains, retains all alternatives, owns
backtracking, and records advice for offline replay. Advice can change cost and
search order. It cannot turn an estimate into a proof, hide an unfinished
alternative, invent a caller contract or decide an ambiguous source result.

Arbitrary transformation proposals need a separate checked transformation
interface. They are not accepted through the current branch-advice protocol.
This keeps the trusted mechanism small enough to inspect while leaving space
for much more ambitious proposal generators.

## No mandatory intermediate-language boundary

A resolved Vexel projection could be useful for inspecting or reusing a region.
It would describe established interfaces, remaining runtime inputs, dependencies
and assumptions. It would have the same status as another inspectable projection
of the resolution graph, rather than becoming a new authority for semantics.

The current projections are the resolution report, dependency graph, native
filter project and editable target project. They can coexist at different
degrees of closure. Target information may arrive before a source region closes;
compiled work may answer a subquestion while other questions remain pending.

That is the closed architectural principle: every answer states what it proves,
for which context, and what still has to be discharged before a consumer may
use it. More powerful inference, additional memory spaces and better optimization
strategies extend the set of questions the implementation can answer. They do
not require relaxing the meaning of a completed answer.

## Checking the boundary

`make test` exercises the implemented profile, including real Make rebuilds,
optional and structural rollback, delayed API demands, generated host C and
SDCC/megalinker cartridges executed on the independent Z80 core. `make sanitize`
runs the same groups with address, undefined-behavior and leak checking.

`python3 tests/check_closure_oracle.py build/vexel` independently enumerates all
three-alternative mappings to two completed answers or an unfinished proof, all
allowed subsets, three worklist orders and both branch directions: 1,296 cases.
Its expected outcome comes from set membership, without using Vexel's search
implementation. Budget exhaustion has separate regression tests. Passing these
checks establishes the tested contract, not completeness for every pure program.

For a demonstration spanning host solving, compiled acceleration and the target:

```sh
build/vexel examples/sudoku.vx \
  --compiled-search build/sudoku-kernels --verify-search-kernel \
  --emit-msx build/sudoku-project --msx-entry entry \
  --search-report build/sudoku-search.json
make -C build/sudoku-project -j2 simulate \
  SIMFLAGS='--irq-period 997 --expanded-slot'
```

Use a fresh export directory. The host resolves Sudoku with checked native
relation filters; the emitted cartridge returns the solved grid's checksum,
405. Sudoku search itself does not run on the Z80. The editable C/assembly and
Makefile remain in the project, alongside placement and execution reports in
its `build` directory. The simulated workload's stack observation remains scoped
to that ROM and machine configuration.
