# Resolving a program, then learning about its realizations

The next abstraction is a question with a scope and an acceptable kind of
evidence. A value, a physical implementation, and an observation of that
implementation can share dependencies without becoming interchangeable facts.

This is now grounded in an executable experiment. `make msx-experiment` resolves
one Vexel computation, exports two SDCC/megalinker projects, runs their Z80 ROMs,
and compares their resource costs. Verified measurements feed an ordinary
Vexel planning puzzle with an explicit cost policy. This is a bounded candidate
set; it does not provide arbitrary physical planning inside the live worklist.

## What the experiment actually found

`examples/neo8_frames.vx` contains a closed 32-bit computation with materialized
helpers. The experiment calls it for eight inputs. Expected results come from
the Vexel resolver. Both machine-code candidates must produce every expected
result before reaching the completion marker. One candidate uses automatic
temporaries; the other uses static temporaries for `sample` and its helpers.

The workload supplies the static candidate's non-overlap contract: one
foreground caller, no callbacks, and no interrupt calls into the computation.
That contract is not inferred for unrelated applications.

With SDCC 4.2.0, the pinned megalinker, the pinned Z80 core, and a synthetic
interrupt requested every 997 model ticks, the initial run measured:

| Quantity | Automatic temporaries | Static temporaries |
|---|---:|---:|
| Resident RAM, including copied code | 2,086 bytes | 2,250 bytes |
| Emitted ROM section contents | 2,645 bytes | 2,952 bytes |
| Padded ROM image | 131,072 bytes | 131,072 bytes |
| Observed stack use | 93 bytes | 75 bytes |
| Model ticks to completion | 894,009 | 919,694 |

Static storage saved 18 observed stack bytes while consuming 164 more resident
bytes and 307 more ROM section bytes. Its execution took more model ticks.
The data does not establish an overall winner. Neither candidate dominates
the other across these costs. The experiment records this comparison without
replacing either editable project.

These numbers belong to this workload, toolchain and machine model. In
particular, 75 is not a safe new stack reserve. It omits other inputs, other
interrupt schedules and the real BIOS interrupt implementation. Changing
compiler flags can also change all these measurements.

## Four questions that look similar but are different

Consider the question “how much stack does this function need?”

1. **Source semantics:** which activations can overlap? The answer depends on
   calls, callbacks, recursion, interrupt entry and application contracts.
2. **Chosen realization:** where are values stored, what is materialized as a
   call, and which ABI applies? These are implementation decisions.
3. **Observed execution:** what was the lowest stack pointer while this ROM ran
   these inputs under this machine model and interrupt schedule?
4. **Proven bound:** what maximum covers every allowed execution under a stated
   environment contract?

Question 3 can be answered without answering question 4. It supplies a useful
counterexample if an execution exceeds a proposed budget. Passing the workload
does not turn it into a proof of the budget. Question 1 alone is also
insufficient: eliminating recursive activation does not account for register
saves, SDCC spills, arithmetic helpers or interrupt frames.

The same distinctions apply to ROM access. A source operation identifies a
logical object. A realization assigns a segment and access protocol. A test
observes correct bank restoration on particular calls. A stronger contract
would establish restoration over every permitted call and interrupt edge.
None requires a universal pointer type in Vexel.

## Facts need subjects and assumptions

An execution observation has a subject more specific than “the program”:

```
subject = ROM digest + machine model + initial state + workload
claim   = minimum observed SP is 0xF323
kind    = execution observation
```

Here `0xF323` corresponds to the automatic candidate's 93 bytes below `0xF380`.
A different candidate receives a different subject. Discovering that the
static candidate reaches `0xF335` does not retract the automatic observation.
Both facts remain true. What changes is which candidate the planner considers
appropriate for the current objective.

The same approach handles edits. A hand-written assembly replacement changes
the realization identity. Evidence about the old ROM remains historical; it
does not transfer automatically to the replacement. Evidence whose dependencies
did not change may be reusable, but that needs an explicit dependency relation.
The current prototype conservatively rebuilds and reruns complete candidates.

`execution.json` records ROM, plan, placement report, symbol map, runner binary
and harness source hashes. It also records initial RAM fill, HIMEM, slot mode,
interrupt period and stopping rule. These bind the observation to its inputs;
they are not signatures or protection against a malicious toolchain.

A future evidence record should include:

| Field | Purpose |
|---|---|
| Subject and property | Identify exactly what the claim concerns |
| Dependencies | Identify the inputs and prior claims it relies on |
| Assumptions | Restrict its validity, including caller and machine contracts |
| Kind | Distinguish definition, proof, measurement, estimate and proposal |
| Domain | State covered inputs, contexts and configurations |
| Producer | Identify the rule, analyzer, compiler or execution model |

This is more informative than one undifferentiated “resolved” bit. It also
prevents a heuristic estimate from satisfying an obligation that asks for a
bound. The core now separates type-property and value demand, retains independent
field/element/schema promises, and records checked type-equation evidence.
Value facts still bundle established values and effect grades. General typed
evidence domains for arbitrary analyzers remain a further extension.

## Keep candidate decisions outside unconditional source truth

The source facts can remain shared while several physical candidates coexist.
Choosing a static frame is a candidate decision under a non-overlap assumption.
Its RAM cost is a consequence learned from the linked object. Neither should
silently mutate an unconditional source value in another candidate.

There is a particularly important cycle to avoid:

```
source chooses an algorithm from measured code size
the algorithm changes code size
the new size changes the source choice
```

Treating size as an ordinary mutable constant would undermine monotone
resolution. Instead, instantiate a candidate with explicit choices, derive its
consequences, then check whether those consequences satisfy its constraints.
Another choice creates another candidate. A finite search may find a consistent
answer, prove that every admissible candidate fails, or remain unfinished when
the budget runs out. These outcomes require different evidence. Running out of
time cannot establish that no candidate exists.

Target inputs such as physical RAM capacity can still arrive early and join
the ordinary graph. They are properties of the selected machine, unlike the
code size of a realization that has yet to be chosen.

## Compilation becomes one available way to answer a question

SDCC compilation answers questions about the cost and behavior of a C
realization. It is already useful inside the outer experiment. The compiler
does not gain authority to redefine the Vexel computation.

The implemented native relation filter already compiles a repeated pure
subproblem during host resolution. Its input is the finite relation established
by ordinary Vexel arithmetic; per-call differential verification compares it
with the ordinary filter. General graph evaluators could extend this choice
between propagation, reuse and compiled execution. Before accepting the
compiled result, it would need the same source semantics: exact integers where
required, explicit width wrapping, defined traps and identical resource input
snapshots. A host `uint64_t` implementation cannot stand in for an arbitrary
integer evaluator just because it is fast on a sample input.

Accelerator setup also has a cost. Building it makes sense when enough repeated
or expensive work can amortize compilation. Caching would depend on the rule
semantics, specialization inputs, resource digests and accelerator toolchain.
External runtime effects remain runtime effects; compilation does not authorize
executing them during source resolution.

An inspectable Vexel form can describe resolved bindings, remaining runtime
inputs and established claims. It need not be a mandatory point after which
target information stops arriving. Different subgraphs may have different
amounts of information and different useful realizations at the same time.

## Where a proposal generator belongs

A human or LLM can suggest “make this frame static” or “replace this arithmetic
routine with assembly.” The suggestion is a candidate proposal. It must name
its base realization, changes, assumptions and predicted benefit. Deterministic
checks decide what evidence the candidate establishes.

For the current experiment, the acceptance checks are deliberately modest:
the exported contract permits the frame policy, SDCC and megalinker succeed,
placement fits, the modeled execution reaches its marker, and eight results
match the resolver. These checks are useful evidence, not universal semantic
equivalence. An optimizer requiring equivalence must request additional proof
or operate within a transformation whose correctness is already established.

A useful stopping rule is “retain legal, non-dominated candidates found within
the exploration budget.” It need not promise a global optimum. Keep failed
candidates and their diagnostics reviewable, and preserve the last accepted
artifact. The present experiment evaluates exactly two predetermined policies;
it makes no LLM calls and contains no autonomous search loop.

### Optional LLM search provider

LLM guidance can participate throughout the proposed joint resolution, not
only optimize an already completed program. A provider can propose a branch,
rank alternatives, suggest a representation or decomposition, or identify a pure
calculation worth compiling. This accommodates difficult decisions without
requiring a hand-written heuristic for every interaction. It does not require
the LLM itself to be a pure Vexel function.

The initial interface should exchange structured proposals using supported
decision kinds. For example, a frame proposal identifies the invocation, the
automatic/static alternatives, its base context, and the non-overlap obligation
that must be established. The model may request that a property be checked; it
cannot invent a caller contract or count its own explanation as that check.
Free-form rationale is useful for inspection but has no evidential force.

The controller owns the following loop:

1. Form a request from the current revision, assumptions, unresolved questions,
   supported choices, known costs, scoped failures and remaining budgets.
2. Validate a proposal's shape, identities and supported operations. A response
   based on an obsolete revision requires revalidation before exploration.
3. Create an assumption context and propagate consequences. Build or execute
   candidate regions when those operations can answer a relevant question.
4. Check the required semantic and physical obligations. For example, a frame
   may reduce stack use but cause the resident RAM plan to exceed capacity.
5. On a justified conflict, reject the candidate and invalidate its dependent
   answers. Preserve the conflict and its premises; optionally ask the provider
   for a revised proposal. An unfinished analysis is not a proved conflict.
6. Retain verified feasible candidates and continue or stop according to the
   declared objective and budget. Keep an earlier accepted artifact intact.

Backtracking need not restart the whole puzzle. Unchanged dependencies can be
shared, while candidate-specific values, generated helpers and placement
measurements remain tied to their contexts. Proven conflicts may exclude other
candidates with the same conflicting premises. A model's suggestion to exclude
a region of search is only a proposal until the solver establishes that conflict.

A later interface could accept synthesized evaluators or rewrite candidates.
Before their outputs can satisfy semantic obligations, they need an established
correctness-preserving construction, a checked equivalence certificate, or an
exhaustive comparison covering the stated finite domain and relevant observable
behavior. Sample agreement remains scoped execution evidence. The simpler first
interface selects from solver-supported strategies so their meaning is already
defined.

Live model output belongs to the search environment. Record the exact request,
reply, provider configuration, base revision, accepted decisions and validation
results. Offline replay consumes that record without calling the model. A
strict artifact replay also requires the same toolchain and resource snapshots,
execution model and candidate decisions; replaying text alone does not establish
identical measurements. Final artifacts identify the selected realization and
its evidence, rather than depending on an undocumented model conversation.

Model availability affects exploration, not the language definition. Invalid
responses or unavailable services fall back to ordinary search. For finite
supported domains, the fallback retains the remaining alternatives; model
ranking must not discard them or serve as an infeasibility proof. Model calls,
tokens, builds and search work have explicit budgets. Exhaustion reports the
remaining obligations and any verified candidates found, without promising a
globally optimal answer. General program equivalence and arbitrary search
spaces are not made decidable by adding a provider.

This interface, fallback and replay mechanism remain proposals. No model calls,
service dependencies or new source syntax are introduced by this document.

## Implemented boundary

`make simulate` executes actual linked Z80 instructions through the unmodified
[floooh/chips core](https://github.com/floooh/chips/tree/9e88298ce56319953ac7a43213a1120359f7a3a6).
The local model implements NEO-8 register writes, mapped ROM reads, exactly
16 KiB of RAM, and three BIOS stubs. Its synthetic interrupt handler preserves
registers and checks mapper-shadow agreement when an interrupt is accepted.
The [NEO specification](https://aoineko.org/msxgl/index.php?title=NEO_mapper)
defines the register layout and reserved bits modeled here.

Tests cover startup from nonzero RAM, initialized data, expanded slot selection,
arithmetic against the resolver, nested calls across segments, both bytes of
the mapper register, enabled and disabled interrupt states, runtime traps and
an observed stack-budget violation. The harness stops at the first HALT;
application success requires a workload-specific completion check.

There is no real BIOS ROM, VDP, audio, RAM mapper, cartridge wait-state model,
NMI workload or hardware execution in this harness. Those limits are also
written into every execution report. A later full-system or hardware run can
add evidence with its own scope instead of silently upgrading these claims.
