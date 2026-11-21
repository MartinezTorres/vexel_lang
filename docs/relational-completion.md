# Relational implementation completion ledger

The requested objective is to complete all eight milestones below, using one
relational semantic core, C++, Makefiles and an inspectable MSX NEO-8
SDCC/megalinker realization. A milestone is complete only when its acceptance
cases execute; this ledger must not turn a restriction into a completed feature.
The relational frontend is now the default executable. The earlier frontend
is historical source and is not an automatic fallback. Optional compiled acceleration and advisor integration follow the
same correctness contract but are not prerequisites for the eight milestones.

The four-pass closure work is specified by the
[acceptance checklist](closure-checklist.md). The table below names implemented
boundaries; validation evidence is recorded separately below it.

| Milestone | Acceptance boundary | Current implementation |
|---|---|---|
| 1. Aggregate execution | Nested arrays/records, places, copies, calls/captures, output-only captures and runtime joins | Implemented; existing family oracles plus the combined closure workload |
| 2. Type/schema closure | Partial properties, aliases, dependent overloads, cycles, capture discovery and fixed-layout runtime invariants | Implemented for the enumerated cases; unsupported/unanchored proofs remain explicit obligations |
| 3. Control completion | Unrolled/runtime iteration, repeat, returns, break/continue, nested joins and exited paths | Implemented for the enumerated cases, including transfers through bank continuations |
| 4. Boundary state | Persistent/external locations, access graphs and explicit result ownership | Finite occurrences plus bounded repeated streams, checked leases and serial submission gates |
| 5. Language families | Numerics, methods, expression parameters, capabilities, imports/resources and metadata | Executable families plus ordinary-expression configuration, input bounds and address bindings |
| 6. Finite solving | Choices, constraints, uniqueness, guarded search, Sudoku and honest incomplete outcomes | Implemented; target requirements can refine ordinary source choices in the same request |
| 7. MSX realization | Banked editable projects, frame policy, mapper ownership, stack contracts and target feedback | Mapped globals, guarded depth, conditional assembly bounds, bank continuations and measured partition retries |
| 8. Consolidation | One frontend, executable acceptance cases, diagnostics, revisions and no fallback | Root build/test, exact-input request reuse, closure workload and current RFC/backend documentation |

## Four-pass implementation (2026-10-13)

Configuration origins no longer imply priority. Source/import and command-line
requirements become the same equations. Ordinary expressions can compute
requirements, bounds, hardware addresses and stream capacities; the physical
request can constrain those expressions before observation uniqueness closes.

The stream adapter owns retained result slots with non-reused lease tokens and
serializes executions. Mapper storage consumes separately granted segments;
the 16 KiB resident constraint remains in force. Large generated functions use
explicit continuation states across bank cuts. SDCC object sizes can trigger
smaller initial partitions. Assembly stack analysis records external/environment
assumptions and refuses to turn unknown control or unbounded recursion into a
bound. Manual changes invalidate inherited guards and static-frame reasoning.

The normal suite exposed a test helper that supplied two different explicit
budgets while relying on last-option-wins; the helper now supplies one budget.
A missing backend binding retains its contradictory result. Build-directory
normalization fixes dependency files that otherwise lost header dependencies
when switching between root and inner Make invocations.
The root executable is replaced atomically, so rebuilding it while an existing
solve is running does not fail with `Text file busy`. Export retries pin one
executable for consistent provenance. Requested roots remain linked even while
the exported user adapter is empty; measured oversized modules cannot hide
behind linker dead stripping.

The full normal regression passes in `build/passes-regression.log`. The final
generated-name reservation change passes the focused closure and boundary-name
suites in `build/passes-final-closure.log` and `build/passes-final-names.log`.
Conservative assembly analysis of signed hexadecimal SP adjustments, changed
index-register halves and unsupported SP paths passes in
`build/passes-final-backend.log`. Export success, retained roots and preservation
of failed-target evidence pass in `build/passes-export-smoke.log`.
The complete ASan/UBSan run passes in `build/passes-final-sanitize.log`, including
all 56 Python suites and the relational/control core checks, with default leak
checking enabled. It includes the final configuration, generated-name and
assembly-analysis cases. `build/passes-evidence.json` fingerprints the final
implementation, current documentation, logs and acceptance artifacts.

The unchanged Sudoku example passes all six worklist/branch permutations,
alternating declaration order, with no guesses. Every answer agrees with the
independent solution and all 27 row/column/box checks. Evidence is in
`build/passes-sudoku-oracle.json` and `build/passes-sudoku.log`; work ranges from
10,968,337 to 12,848,968 steps. The current executable's SHA-256 is
`f3afe9c2734005c623c80214d91410a1611286a0d10d9949bd515dc8a73ec39d`.

`make closure-msx` passes 64 host and 64 Z80 source activations per profile,
using an independent arithmetic oracle. It combines dependent records, runtime
iteration/continue, recursive native calls, stream ownership and bank cuts.
The mapper profile also checks reads/writes across offsets 16,383/16,384 and
restoration of the cartridge mapping. Required stack proofs reject a missing
BIOS contract and a bound larger than the reservation, without publishing a
successful report. The editable projects and comparison are in
`build/closure-msx/run-bloxxjjw`; the command log is
`build/passes-final-msx.log`.

| Workload | ROM section bytes | Resident RAM bytes | Conditional stack bound | Observed stack bytes |
|---|---:|---:|---:|---:|
| Closure, 16 KiB baseline | 10,603 | 3,765 | 400 | 302 |
| Closure, extra mapper grant | 11,183 | 4,344 | 400 | 288 |
| Oversized function | 35,899 | 2,723 | 232 | 190 |

Each image occupies 131,072 bytes including ROM padding and reserves 1,024
stack bytes. Bounds include an explicit 64-byte embedding allowance. The closure
workload bounds generated recursion at depth 12; the mapper case contracts the
model's ENASLT stub at zero additional bytes. This is conditional evidence for
the linked code and model, not an installed MSX BIOS or whole-application proof.

The oversized-function workload rejects 128-operation groups (largest module
28,176 bytes), then 64-operation groups (13,840 and 12,336 bytes), and accepts
32-operation groups. Its six continuation modules range from 790 to 6,672 code
bytes. Six host inputs agree with an independent oracle; the Z80 run visits
every continuation bank and agrees with that oracle. Evidence and the editable
project are in `build/bank-pressure-verified`, particularly `realization.json`
and `build/comparison.json`. Run `make bank-pressure-msx` to reproduce the full
experiment. The earlier `build/bank-pressure-project` experiment was invalidated
by dead stripping and is not acceptance evidence.

The final assembly-analysis review preserves all three ROMs, execution results
and stack bounds. Each project has `build/stack-review.log`; its resolution and
comparison identify the current analysis tools by SHA-256. Earlier checkpoints
below are historical evidence, not claims about the current bytes.


Initial checkpoint: 21,977 relational core assertions; 3 joint, 16 source and
7 execution test groups; source/control oracles; five MSX projects. The existing
tests cover scalar call state and runtime recursion, not the full milestones.

## Incremental workspace checkpoint (2026-10-12)

`SourceWorkspace` and `--session` retain closed request groups with exact input
support. The reuse key includes the entry path, requested observations, boundary
contracts, target budgets and search options. Source/import contents, demanded
resource bytes, observed directory membership and path-selection facts are
validated before reuse. Selection includes absent leaves behind symlinked
parents: a missing path's resolved destination is a premise even while its
existence remains false. Invalid edits do not return the old valid projection.
Process-dependent requests and shared-budget requests bypass the cache.

The implementation deliberately uses whole-request granularity. A source edit
invalidates groups containing that file in their parsed import closure; a
resource edit can preserve unrelated groups on the same entry. Reuse within a
request, persistent caches across process restarts and general process-input
declarations remain open. This checkpoint adds no language keyword or mandatory
lowering barrier. The RFC and architecture describe the implemented contract.

The normal regression passes in `build/incremental-regression.log`. The
subsequent absent-parent-path correction passes the incremental, resource and
module suites in `build/incremental-input-delta.log`, including comparisons with
fresh solves. The complete ASan/UBSan suite passes in
`build/incremental-sanitize.log`; the final path-support correction also passes
the focused sanitizer suites in `build/incremental-input-sanitize.log`, with
default leak checks enabled. The incremental tests also cover signature additions/removals,
ambiguous results, invalid-edit recovery, same-size/same-timestamp byte edits,
target-contract changes, process behavior, bounded retention and emitted-file
equality. `--verify-incremental` compares retained reports and projections with
a fresh derivation, reporting verification work separately from reuse.

The full Sudoku example also passes a first solve, a reused request and an
independent verification solve in `build/incremental-sudoku.json`. The first
solve uses 11,998,216 steps and zero guesses; reuse uses zero solver steps, with
the separate verification consuming another 11,998,216 steps. All 27 row,
column and box checks pass. This checkpoint uses the default solver order; the
earlier language checkpoint below records the six-order Sudoku oracle.

`make incremental-msx` replays eleven requests through one live process, with
four reuses that avoid 32,588 solver steps. Every step's report and emitted files
agree with an independent clean invocation. Editing a palette preserves the
tone request; editing the common imported source conservatively rebuilds both.
The resulting editable SDCC/megalinker projects pass 1,024 host and 1,024
simulated Z80 activations against independent arithmetic checks. Evidence is in
`build/incremental-msx/run-3uw2cllh/comparison.json` and
`build/incremental-msx.log`, with executable, driver, packager and input hashes.

| Local storage plan | ROM section bytes | Resident RAM bytes | Observed stack bytes |
|---|---:|---:|---:|
| Automatic | 3,801 | 2,492 | 85 |
| Exclusive, 128-byte budget | 3,745 | 2,512 | 69 |

Both cartridge images occupy 131,072 bytes including ROM padding, target the
16 KiB physical RAM baseline, and reserve 1,024 bytes for stack. The workload
computes sprite state and tone data; it does not exercise VDP/audio hardware.
Observed stack use is not a bound. The earlier exploratory run exhausted a
100-million-tick simulation limit; the final replay uses an explicit
one-billion-tick limit and completes at about 139 million model ticks. Generated
code still includes repeated runtime remainder calls; this checkpoint measures
the workload's costs without claiming target optimization is complete.

## Current language revision (2026-10-12)

The approved [capability/interface revision](language-simplification.md) replaces
semantic optional blocks, removes recursive capture-mode enumeration, and
publishes immutable lexical declaration sets. Changing native accumulator
constants generalize at recursive edges; fixed shapes retain invariant facts.
The root Makefile now builds/tests one supported frontend, and architecture
rules permit an ordinary parser and inspectable IR.

The historical evidence below applies to its named snapshots, **not** to this
revision. In particular, optional rollback is no longer an acceptance criterion.
Normal regression coverage passes in `build/simplification-regression.log`
and `build/simplification-regression-tail.log`. The tail run supersedes one
initializer fixture that assumed an exact helper-sharing plan; the revised
fixture still demands physically shared locals and verifies their overlapping
activation safety, along with execution results. The complete sanitizer suite
passes in `build/simplification-sanitize.log`, including the updated fixture,
with address, undefined-behavior and default leak checks enabled. The direct
`make sanitize SANITIZE_LOG=...` invocation reused the existing approval.

The revised capability/state workload passes 3,072 host and 3,072 simulated Z80
activations with automatic and exclusive local storage. Its source, executable
and driver fingerprints match
`build/relational-capabilities-msx/run-qdu2e_yj/comparison.json`; the generated
SDCC/megalinker projects remain editable in that run directory.

The full Sudoku oracle passes all six worklist/branch-order combinations and
alternates declaration order, with guessing disabled. Its independent solution
and row/column/box checks are recorded in
`build/simplification-sudoku-oracle.json`, matching the current executable and
source. This exposed and fixed an earlier control-refinement bottleneck:
requiring a whole aggregate to become concrete before exposing its elements
blocked finite propagation. Refinement now preserves integer relationships and
works leaf by leaf. The full example uses an explicit twenty-million-step
budget; the CLI default remains ten million.

## Verified progress during the completion work

Finite-word proofs now include division/remainder with proved nonzero divisors
and variable shifts with proved valid counts. Signed truncation and wrapping,
invalid-operand opacity, optional rollback, initialization and actual traps are
checked by 28,990 control-proof assertions and five new source groups under all
six traversal orders. Existing numeric-control, runtime-readiness, numeric-loop
and optional suites pass. The expanded workload passes 17,408 host and 17,408
Z80 activations in `build/relational-optional-msx/run-3e616avg/comparison.json`;
its three fingerprints match the candidate and corrected target fixtures. The
first C oracle required an unlinked SDCC division helper; the corrected oracle
uses independent repeated subtraction. The full regression passes with 23,903
core assertions, 28,990 control-proof assertions and 267 Python test groups;
all 255 inputs and the executable match
`build/relational-total-arithmetic-regression.json`. The driver-only correction
was integrated after that hash check and matches the passing target evidence,
as recorded in `build/relational-total-arithmetic-driver-delta.json`.
The isolated sanitizer suite also passes, including address, undefined-behavior
and leak checks; all 255 snapshot hashes match
`build/relational-total-arithmetic-sanitize.json`. The eight milestones remain
incomplete. The latest sanitizer invocation reused the
existing `make sanitize` approval with `SANITIZE_LOG` and full leak checks.

Boundary exports now reject C names that collide with generated initializers,
load/store helpers, external bridges or materialized functions. Logical Vexel
names stay available through a distinct `--occurrence` name. The frame graph
also rejects duplicate physical node names instead of silently dropping one.
Fourteen focused groups pass against the full initializer-ownership baseline;
the 254-input record is `build/relational-boundary-names-regression.json`.
The subsequent total-arithmetic sanitizer checkpoint covers this change.

Current follow-up: lazy initializer calls now participate in per-function
activation ownership. Loads and first stores add their initializer's actual
call path, so helpers shared with overlapping sources retain automatic locals.
Initializer cycles remain visible; resident initializer temporaries stay
automatic. Two new regression groups and the existing frame, boundary, contract
and bank groups pass under all six traversal orders.

The optimized candidate passes 7,676 host and 7,676 Z80 activations across
fourteen projects in `build/relational-occurrences-msx/run-fsnpagnn/comparison.json`,
with all three fingerprints checked against the candidate and fixtures. Its
full regression passes with 23,903 core and 13,376 control-proof assertions,
plus 259 Python test groups. All 253 input hashes and the executable match
`build/relational-initializer-frames-regression.json`; its isolated sanitizer
suite also passes, with all 253 snapshot hashes checked in
`build/relational-initializer-frames-sanitize.json`. The preceding
per-function checkpoint passed its existing suite but omitted initializer
edges; it remains marked `suite-passed-known-bug` in
`build/relational-occurrence-frames-regression.json` and its sanitizer record.
Its earlier target and sanitizer results do not cover the newly found ownership
error; the later checkpoints above include the fix.

Finite named source occurrences now have independent
invocations and result owners. Explicit begin/produce/release edges determine
execution order and result reuse; repeated source definitions and nested
callbacks use the same graph. Seven occurrence groups pass their focused tests.
The target workload passes 4,604 host and 4,604 Z80 activations across eight
projects in `build/relational-occurrences-msx/run-tjrznd0l/comparison.json`, with
all three fingerprints checked against `build/relational-boundary-occurrences-verified`
and the fixtures. The full regression passes with 23,903 core and 13,376
control-proof assertions; all 251 input hashes and the executable match
`build/relational-boundary-occurrences-regression.json`. Its isolated sanitizer
suite also passes; all 251 snapshot hashes match
`build/relational-boundary-occurrences-sanitize.json`.
Per-function refinement of overlap is subsequent work, not part
of this evidence.

Initialization proofs and executable readiness are now
separate facts. A completed loop exposes its actual readiness flag to later
execution; merges and capture/optional boundaries preserve both facts. This
repairs the guarded-division probe whose normalized proof formula previously
evaluated division outside the source guard. Seven dedicated groups cover
partial operations, nested loops, captures, native calls, rollback, real traps
and uninitialized reads. The focused loop/optional/capture/type suites pass.

The optimized candidate passes 7,168 host and 7,168 Z80 activations in
`build/relational-loop-init-msx/run-ky1oz2zw/comparison.json`, and 15,360 of each
in `build/relational-optional-msx/run-7vow9xfk/comparison.json`. Both records match
`build/relational-runtime-readiness-verified` and the fixture fingerprints.
The full regression passes with 23,903 core assertions and 13,376 control-proof
assertions; all 248 input hashes and the executable match
`build/relational-runtime-readiness-regression.json`. The isolated sanitizer
suite also passes; all 248 snapshot hashes match
`build/relational-runtime-readiness-sanitize.json`. The preceding numeric checkpoint's evidence remains marked
with its known bug; these results do not retroactively repair that executable.

Native receiver certificates retain logical roots and designated paths. Shared
and overlapping receiver views observe the same writes, and later argument
effects cannot change a previously selected index. Temporary receivers retain
their converted types and private roots. The physical call and its traps remain
in the execution graph.

Five receiver groups pass under all six traversal orders. The full normal suite
passes with 23,903 core assertions and 12,857 control-proof assertions; all 246
input hashes and the frozen executable match
`build/relational-native-receiver-control-regression.json`. Thirty workload
sources pass 15,360 host and 15,360 Z80 activations in
`build/relational-optional-msx/run-azsp4wj2/comparison.json`, with all three
fingerprints checked against `build/relational-native-receiver-control-verified`
and its fixtures. The isolated sanitizer suite passes, with all 246 input
hashes checked in `build/relational-native-receiver-control-sanitize.json`.
The later guarded-division readiness regression is outside this snapshot.
This does not supply general recursive summaries or numeric loop induction.

Native result certificates now include automatic captures. The proof receives
only the incoming types/values supplied by the real interface, closes all
returned writes and their initialization, and preserves physical result-slot
order even for a static or `NoValue` return. Earlier call certificates compose
without replacing the original execution. Type-only and output-only access do
not invent an incoming initialized value.

Nine capture-control groups pass under all six traversal orders, including
changing captures, aggregate slot order, successive updates, source-choice
ambiguity and traps. The full normal suite passes with 23,903 core assertions
and 12,857 control-proof assertions; all 245 input hashes and the executable
match `build/relational-native-capture-control-regression.json`. The expanded
workload passes 14,336 host and 14,336 Z80 activations in
`build/relational-optional-msx/run-mpr9kl1_/comparison.json`, matching the frozen
`build/relational-native-capture-control-verified` executable and both fixture
fingerprints. The isolated sanitizer suite passes, with all 245 input hashes
rechecked in `build/relational-native-capture-control-sanitize.json`. Receiver
certificates and broader recursive/control closure are subsequent work, not
covered here. The later partial-guard execution probe is outside this test set.

Native scalar and aggregate return slots now admit a sufficient semantic
certificate from the actual call arguments. A private body execution must close
its result and execution obligations. Its facts feed control proofs without
replacing the physical call, its stores, traps or initialization effects. Finite
source alternatives remain public obligations, and nested private calls remain
opaque.

The full normal suite passes with 23,903 core assertions, 12,857 control-proof
assertions and eight native-control groups. All 244 snapshot inputs and the
executable match `build/relational-native-control-regression.json`. The expanded
twenty-five-source workload passes 12,800 host and 12,800 Z80 activations in
`build/relational-optional-msx/run-84w3ph52/comparison.json`, with all three
fingerprints checked against `build/relational-native-control-verified` and the
source/driver. The example needed a local helper rename to avoid an existing
equally specific overload; that fixture-only correction and the README command
spelling follow the normal-suite snapshot. The independent sanitizer suite
passes, with all 244 snapshot hashes rechecked in
`build/relational-native-control-sanitize.json`. This evidence predates the
capture and receiver extensions. Automatic capture certificates, receiver
certificates and general recursive results remained open at this checkpoint.

Numeric control now shares finite bits across native arithmetic, casts and
comparisons, preserving signed ordering and wrapping. Optional validation
distinguishes an impossible absence path from an unproved one. Opaque native
results cannot manufacture an absence counterexample. Closed loop execution
supplies sufficient first-iteration evidence from the actual initial values
and elements; changing loop bits still lose their identity at back edges.

The full normal suite passes with 23,903 core assertions, 12,857 control-proof
assertions and six new numeric-control groups. All 242 inputs and the frozen
executable match `build/relational-numeric-control-regression.json` and its
manifest. The twenty-two-source workload passes 11,264 host and 11,264 Z80
activations in `build/relational-optional-msx/run-9sedt11q/comparison.json`,
matching `build/relational-numeric-control-verified`. The corrected sanitizer
snapshot passes, with all 242 input hashes rechecked in
`build/relational-numeric-control-sanitize.json`. Its first revision caught the
typed loop-location lookup gap and was not a passing checkpoint. This evidence
predates native result certificates.

At that earlier checkpoint, native call results still lost semantic relations
across the materialized interface. The sufficient result certificate above
addresses part of that gap. Later-iteration
numeric induction, variable shifts, division and non-integer control proofs
also remain open. These checks do not close the eight milestones.

Execution-dependent optional type queries now carry typed absence completion,
including conditional results and early returns. Their lexical query context
retains the actual reaching state and control premises. Conditional property
selection and execution use the same premises; sibling branches and later
writes cannot borrow an earlier value's guard. After a sufficient universal
function-interface proof fails, the actual invocation keeps its runtime
expressions instead of substituting unrelated generic signature inputs.

The full normal suite passes with 23,903 core assertions and twenty optional
groups in `build/relational-optional-guard-regression.log`; all 238 input hashes
and the frozen executable match `build/relational-optional-guard-regression.json`.
The nineteen-source workload passes 9,728 host and 9,728 Z80 activations in
`build/relational-optional-msx/run-bw47_s68/comparison.json`, matching
`build/relational-optional-guard-verified`. The isolated sanitizer suite passes
with all 238 snapshot hashes checked in
`build/relational-optional-guard-sanitize.json`. This checkpoint predates numeric
control proofs. General arithmetic/control feasibility and mixed recursive absence
remain open; this does not close the eight milestones.

Optional type queries now detect closed missing overloads, arity mismatches and
unavailable fields returned by helpers. The field route shares ordinary query
completion and reaching-state selection. Fallible matching demands only the
argument facets needed by dispatch, preserving partial schemas and required
arithmetic, ambiguity and unfinished-type obligations.

The full normal suite passes with 23,903 core assertions and twelve optional
groups in `build/relational-optional-query-regression.log`. Its executable and
all 238 snapshot file hashes match the record in
`build/relational-optional-query-regression.json`. The fifteen-source workload
passes 7,680 host and 7,680 Z80 activations in
`build/relational-optional-msx/run-v6s9y2ch/comparison.json`, matching
`build/relational-optional-query-verified`. The isolated sanitizer suite passes
with all 238 snapshot hashes rechecked in
`build/relational-optional-query-sanitize.json`. This checkpoint predates execution-dependent query completions and
the work on preserving enclosing control premises through those queries.

Recursive capture cycles now search finite slot and access-mode proposals in
isolated proofs. Omitted roots retain watched type/value views and initialization
facts. A completed footprint only proposes an interface; the recursive graph
must close again with that interface's returned state. The search can certify
unused, type-only, read-only, output-only and read/write captures, including a
first call that initializes a value for later recursive calls. It shares the
request's budget and does not promise an optimal ABI.

Seven recursive-capture groups and the six existing capture-mode groups pass
under all six traversal orders. The new workload passes 3,072 host and 3,072
Z80 activations in
`build/relational-recursive-captures-msx/run-89z_7i6u/comparison.json`, matching
`build/relational-recursive-modes-verified`. It observes 303 versus 285 stack
bytes with automatic and selected static locals; these are not stack bounds.
The expanded function-type workload passes 3,584 host and 3,584 Z80 activations in
`build/relational-function-types-msx/run-0tzbob7z/comparison.json` with the same
executable. Its unused capture-signature facets no longer distinguish equivalent
observable graphs. The full normal suite passes with 23,903 core assertions in
`build/relational-recursive-modes-regression.log`; executable and code/test input
fingerprints were checked in `build/relational-recursive-modes-regression.json`.
The isolated sanitizer suite also passes with all 238 input hashes rechecked:
`build/relational-recursive-modes-sanitize.json`. This sanitizer evidence predates
the optional-query changes above.
The eight milestones remain incomplete.

Observable uniqueness now closes the reachable native function and persistent
storage graph. Demanded return/capture leaves and execution effects participate;
unused return leaves, arbitrary local IDs and output-buffer extents do not.
Empty and `NoValue` sources retain their effects. Logical output keys remain
stable when an unused return shape shifts the captured-output ABI slots.
Type-only queries keep their smaller observation boundary.

`build/relational-observation-regression.log` passes the full suite with 23,903
core assertions and five observation groups under all six traversal orders.
The expanded function-interface workload passes 3,072 host and 3,072 Z80
activations in
`build/relational-function-types-msx/run-3gk7g9pt/comparison.json`, matching
`build/relational-observation-verified`. The six independently checked Sudoku
runs also agree; their fingerprints are preserved in
`build/relational-observation-sudoku.json`. AddressSanitizer,
UndefinedBehaviorSanitizer and leak checks pass on the isolated 235-file
snapshot; every input hash was rechecked in
`build/relational-observation-sanitize.json`. This checkpoint predates recursive
capture-interface search and the subsequent unused-signature observation fix.
Normalized residual graphs do not decide arbitrary extensional equivalence.

Private native-interface proofs no longer discard a finite source alternative
under abstract inputs and publish its surviving competitor as a universal fact.
The constrained and ambiguous array-extent cases pass all six search orders.
The complete suite passes in `build/relational-abstract-choices-regression.log`;
the function-interface workload passes 2,048 host and 2,048 Z80 activations in
`build/relational-function-types-msx/run-yoj04gg9/comparison.json`, matching
`build/relational-abstract-choices-verified`. AddressSanitizer,
UndefinedBehaviorSanitizer and leak checks pass on the isolated 231-file
snapshot, with all input hashes rechecked:
`build/relational-abstract-choices-sanitize.json`.
This checkpoint predates the subsequent observable artifact-graph work.

Optional places, type-reference prerequisites and fallible function signatures
pass the complete normal suite in
`build/relational-optional-signatures-regression.log`, including eleven optional
groups. Missing destinations stop dependent operations, while invalid indices
and earlier traps remain obligations. Partial type queries retain only their
demanded fields. Overload matching checks needed signature components without
closing an inapplicable candidate's unused tail. Missing return annotations
preserve body completion and caller-directed escapes.

The thirteen-source optional workload passes 6,656 host and 6,656 Z80 activations
in `build/relational-optional-msx/run-i_iiwnam/comparison.json`. The isolated
231-file snapshot also passes AddressSanitizer, UndefinedBehaviorSanitizer and
leak checks, with every input hash rechecked:
`build/relational-optional-signatures-sanitize.json`. These results predate the
subsequent abstract-proof source-choice fix. They do not cover every optional
type-query interaction or mixed recursive absence.

Optional semantic transactions now pass the full normal suite in
`build/relational-optional-encoding-regression.log` (23,861 core assertions,
seven optional groups and three output-encoding groups). Successful validation
reasserts the original relationships; proved absence discards staged program
state and finite requirements while retaining the facts that justify rollback.
Prefix errors, ambiguity, unresolved proofs and exhaustion remain distinct.
Nested transactions, runtime control, successful recursive optionals and
changing captured string premises have independent host execution checks.
Type-level absence, missing member destinations and mixed recursive completion
still require further implementation; this does not complete milestone 5.

`make relational-optional-msx` passes 4,096 host and 4,096 Z80 activations with
automatic and selected static locals. Evidence in
`build/relational-optional-msx/run-lmcnswql/comparison.json` matches
`build/relational-optional-verified`; observed stack use is 265 versus 255 bytes,
not a bound. The initial target attempt exhausted ten million work units while
enumerating equivalent output encodings. A dominance proof for the current
exclusive byte arena retains the existing encoding preference and all joint
constraints while avoiding that product. The same example resolves in 193,081
work units. Shared lifetimes retain joint search, and differing source
observations remain ambiguous. AddressSanitizer, UndefinedBehaviorSanitizer and
leak checks also pass on an isolated 230-file snapshot; every input hash was
rechecked (`build/relational-optional-sanitize.json`). This evidence predates
the subsequent optional place, type-reference and function-signature changes.

Captured location types, incoming values and returned writes now have distinct
interfaces. The private capture proof closes all three requirements together.
Read-only captures return no slots; whole-root setters need no incoming value;
type-only captures retain the actual reaching type through nested helpers.
Uninitialized storage remains distinct from typed storage, including in loops.
Partial stores and actual reads retain their initialization obligations.

The full normal suite passes in `build/relational-capture-modes-regression.log`
(23,861 core assertions plus six new groups under all six traversal orders).
`make relational-capture-modes-msx` passes 4,096 host and 4,096 Z80 activations;
`build/relational-capture-modes-msx/run-21imarq0/comparison.json` matches
`build/relational-capture-modes-verified`. Observed stack use is 93 versus 69
bytes with automatic versus selected static locals, not a bound. AddressSanitizer,
UndefinedBehaviorSanitizer and leak checks pass on an isolated 225-file snapshot;
all input hashes were rechecked (`build/relational-capture-modes-sanitize.json`).
This evidence predates optional semantic transactions. Recursive capture minimization,
general conditional initialization and the larger milestones remain open.

Semantic native-capture selection passes the full normal suite in
`build/relational-capture-regression.log` (23,861 core assertions and four new
capture groups). A private proof can remove captures belonging to unselected
overloads and branches, including through nested native calls. It preserves
static facts used by type dimensions, changing native captured values, and
output-only receivers. Captures are observed as candidate-local artifacts;
absence is accepted only after the proof closes. Supplied caller properties
are explicit proof inputs, while canonical internal relationships retain their
own obligations. Private frame cache entries are isolated by proof instance.

`make relational-capture-closure-msx` passes 3,072 host and 3,072 Z80 activations
with automatic and selected static locals. Evidence is in
`build/relational-capture-closure-msx/run-dusuquej/comparison.json`, matching
`build/relational-capture-verified`. Observed stack use was 92 and 62 bytes,
respectively; these measurements are not whole-program bounds. AddressSanitizer,
UndefinedBehaviorSanitizer and leak checks pass on an isolated 222-file snapshot,
with all input hashes rechecked (`build/relational-capture-sanitize.json`).
This checkpoint predates capture input/output modes. Recursive capture-discovery
cycles still retain conservative requirements at the recurrence, and expression
parameters keep their existing capture route; the larger milestones remain open.

The function-interface checkpoint passes the full normal suite in
`build/relational-function-type-regression.log`, with four focused groups across
all six search/declaration traversal orders. Unknown typed arguments can supply
a common unannotated result interface without supplying a value. Private
symbolic inputs never become program values, and a failed universal proof falls
back to the actual invocation. Partial field queries retain their smaller
dependency sets. Automatic captures and non-native argument interfaces still
require further closure work; this does not complete milestone 2.

`make relational-function-types-msx` passes 2,048 host and 2,048 Z80 activations
with automatic and selected static local buffers. It checks type-only sink
elision, aggregate interfaces, concrete heterogeneous branches, and recursive
helpers used both for type queries and actual execution. Evidence:
`build/relational-function-types-msx/run-xvfyw7uc/comparison.json`, fingerprinted
against `build/relational-function-types-verified`. The full sanitizer suite
passed from an isolated 218-file snapshot whose input hashes were rechecked;
evidence is in `build/relational-function-types-sanitize.json`. That snapshot
predates the subsequent semantic capture-closure work.

The independent core now has finite integer domains, domain-aware unification,
reversible clause transactions and dependency-driven wakeups. Fresh interface
variables keep existing variable roots instead of forming long alias chains.
The core still has no Vexel type tags or source evaluator built into its search.
The last complete regression checkpoint contains 23,711 core assertions and
3 joint, 17 source, 7 execution, 4 aggregate, 2 completion, 3 finite-region,
4 expression/completion, 5 boundary-state, 14 type-closure and 4 schema-invariant
test groups (`build/relational-control-schema-regression.log`). This checkpoint also passed AddressSanitizer, UndefinedBehaviorSanitizer and
leak checks (`build/relational-sanitize.log`).

Type equality proofs use isolated requests with opaque interfaces, including
property bundles discovered by delayed builders. A helper must prove its type
relationship without borrowing an initializer's representation. Direct alias
components join all supplied interfaces; record members and array properties
retain their logical owners, so a partial query sees the joined field type
without demanding an unrelated schema. Reaching-state member tests also check
captured writes through helper calls. Nested proofs share the request's work
and decision budget. Transforming equations revise a common candidate vector
in isolated requests, recheck supplied witnesses in each revision, and validate
the original query exactly before publishing a candidate. Provisional overload
failures and arithmetic traps are reconsidered after other equations change
the vector. Partial record projections use the same mechanism without closing
unrelated siblings. A generic named-input obligation lets a proof request facts
from its enclosing execution; the adapter waits through ordinary graph builders
and then retries with explicit input equalities. It does not inspect or borrow
the enclosing context's binding store.

`make relational-types-msx` checks 512 host and 512 Z80 activations, exercising
revised record return frames, global arithmetic equations and partial schema
proofs. Evidence is in
`build/relational-types-msx/run-ykqcnajb/comparison.json`. This verifies execution
of this type-closure profile, not full language or backend completion.

Schemas now validate fixed field relationships during construction and updates.
Checks run against readonly field views and frozen lexical properties. Capture
discovery includes alternatives that a later field update can select, so two
instances cannot borrow one another's captured environment. Helpers may modify
their own scratch state; they cannot mutate schema inputs or invoke sinks.
Straight-line dimensions also support exact fields without inventing a runtime
representation. General branching exact-field dependency proofs remain open.

`make relational-schema-msx` passes 1,536 host activations, 1,024 valid Z80
activations and two deliberately invalid Z80 programs reaching invariant trap
19. Evidence: `build/relational-schema-msx/run-crlrb9ta/comparison.json`. This
check exposed a shared-operand error in SDCC's Z80 byte arithmetic. The current
C projection preserves residual roots in automatic storage; the local
reproducer and realization cost are documented in [sdcc-operands.md](sdcc-operands.md).
Boundary-result validation and further control interactions pass their host checks;
the recorded executable fingerprints must match before reusing any evidence.

Aggregate interfaces include nested arrays and records, logical places,
structural copies, runtime recursion and output-only captures. The aggregate
MSX oracle checks 2,304 host and 54 Z80 inputs. Runtime loop interfaces use
parallel updates of carried values, with separate completion guards and exit
values. The completion MSX oracle checks 4,096 host and 846 Z80 inputs, including
continue and return in predicates, iterable/deferred transfers and an early
record return. Evidence: `build/relational-completion-msx/run-yar0q9bf/comparison.json`. These are bounded execution checks, not
stack-bound proofs or full backend completion.

Persistent accesses retain fresh runtime snapshots; whole assignments and
logical member/index updates preserve RHS ordering. Lazy initializers run once
on first access, with separate absent/ready/initializing states and checked
uninitialized reads and reentry. Pure unresolved cycles remain unresolved.
External functions use an inspectable flattened C bridge, and external storage
supports named symbols and MSX address/port bindings. Type queries do not access
the storage. Void sinks retain execution without allocating output payload.
Transitive resource uses include callees and initializer dependencies. Shared
mutation under an overlapping activation contract remains open until access
ordering is supplied; independent persistent resources may overlap. The default
external-world resource is deliberately conservative, not an inferred claim
that distinct sinks are independent.

`make relational-boundary-msx` checks 256 host and 256 Z80 activations, including
lazy initialization, partial aggregate writes, sink calls and a materialized
helper. The current evidence is in
`build/relational-boundary-msx/run-yy3quauh/comparison.json`; the workload observed
94 stack bytes, not a whole-program bound. As with all generated evidence,
the recorded executable fingerprint must match before reusing the result.

The schema/control checkpoint also reran the source, control, execution, aggregate
and completion MSX oracles. Their current run directories are, respectively,
`run-fsii5q_q`, `run-xv2v2rtp`, `run-98nvbosf`, `run-vfj37pv0` and `run-yar0q9bf`
under the corresponding `build/relational-*-msx` directories. The six Sudoku
permutations were refreshed with the same executable as well.

`make relational-sudoku-oracle` solves `examples/sudoku.vx` under six search
orderings and alternates the placement of the clues declaration. Every run
produces the same complete grid, which an independent Python check validates
against all row, column and box sets. The first measured run took 5.53 seconds,
about 499 MiB of peak resident memory and 3,279,506 work units, with no search
decisions. The exact evidence is fingerprinted in
`build/relational-sudoku-oracle.json`; fresh evidence is required after changing
the executable. Finite-region tests separately cover ambiguity, inconsistency,
unfinished proofs, budget exhaustion, guarded constraints and invalid candidates.

Remaining work is not hidden by the new tests: the broad type machine,
finer boundary contracts, the omitted language families, whole-program target
planning and consolidation must still close before this ledger can say done.

## Operator and partial-category checkpoint

`build/relational-category-regression.log` records a passing complete normal
suite with two additional operator groups. This checkpoint separates boundary
effects from runtime computation: pure materialized helpers and runtime loops
can check record invariants, while finite solving still requires compile-time
completion. Frame effects come from transitive accesses, including reused
materialized helpers. Schema instances cannot reuse an unchecked ordinary frame.

The outer array/record/scalar category is an independent type projection. This
lets `|expr|` distinguish array length from integer absolute value without first
closing an array's element and extent. Opaque type proofs and frozen schema
captures preserve this partial information. Seedless dimension equations still
pass all declaration and search permutations.

The current scalar profile now includes bitwise operators, independent shift
count types, logical short-circuiting and all scalar compound assignments.
Discarded divisions, shifts and indexed updates retain their trap obligations;
logical compounds gate both the RHS and the write after evaluating the place.
`make relational-operators-msx` passed 1,024 host activations, 776 valid Z80
activations and two expected trap ROMs. Evidence:
`build/relational-operators-msx/run-f82lxr3k/comparison.json`. This workload took
125,652,763 model ticks and observed 65 stack bytes; neither is a general bound.
The sanitizer checkpoint above predates these changes and must be refreshed.

The subsequent normal checkpoint is `build/relational-wide-regression.log`:
23,711 core assertions, 15 type-closure groups (one added for independent outer
categories and member shapes), two operator groups and two target-layout groups,
alongside the previously listed source/execution families. Record prototypes,
joined record members and annotated external results expose array shapes
without demanding unrelated element types. Member queries retain an enclosing
function's return owner when a required prefix transfers control.

Unsigned runtime outputs can now select 32-bit physical storage as well as the
existing 8- and 16-bit candidates. Absolute memory/port bindings are preserved
in `source-plan.json` and copied into the editable target plan. The linked
placement checker separates port and memory spaces and rejects bindings that
overlap linked resident RAM or reserved stack. Explicit aliases of external
locations remain possible. This is collision feedback, not automatic relocation
around reserved holes or a complete mapper-memory allocator.

`make relational-bindings-msx` verifies 256 host and 256 Z80 activations using
a native four-byte `#u24` location ending exactly at the stack boundary, then
links and rejects two overlapping candidates. Evidence:
`build/relational-bindings-msx/run-r1xmz706/comparison.json`.

The wide-output/partial-projection checkpoint also passed AddressSanitizer,
UndefinedBehaviorSanitizer and leak checks in `build/relational-sanitize.log`.
Subsequent allocation changes need their own validation before reusing that
checkpoint's executable evidence.

The output allocator now derives canonical placement equations for its simple
byte arena from the closed lifetime graph: pairwise exclusive outputs share a
base; simultaneously live outputs are packed contiguously. Mixed and open
lifetimes keep general placement constraints. This removes equivalent offset
choices without pruning semantic alternatives. Four full-range 32-bit outputs
resolve under all six search orders with a 1,000-decision budget. Empty producers
complete without a result or a storage interval.

The complete normal suite passes in `build/relational-packing-regression.log`
(18 source groups). The widened operator workload passes in
`build/relational-operators-msx/run-ylgir8cu/comparison.json`, and the linked
binding workload passes in `build/relational-bindings-msx/run-gjtnjp59/comparison.json`.
The sanitizer result above predates this packing/empty-block change.

Character literals and constant byte strings now have source relationships,
including binary-safe contents, unsigned lexical comparisons, overload matching,
string identity casts and bounds-checked runtime indexing. String extent is a
value property; sharing `#s` does not equate two strings' lengths. Known cells
can retain independent frozen properties, including category and extent. A
string member's element type can resolve without its value, while its length
requires owner execution. Frozen string schema captures survive later local
assignment. Materialized string call interfaces, resources and string exports
still need their integration work.

The full normal suite, including three string groups, passes in
`build/relational-strings-regression.log`. `make relational-strings-msx` checks
512 host and 259 valid Z80 activations and a discarded out-of-bounds read reaching
trap 16. Evidence: `build/relational-strings-msx/run-n30x_q77/comparison.json`.

Signed integer relationships now cover constants and runtime values, same-sign
promotion, checked implicit conversion, explicit wrapping casts, comparisons,
arithmetic right shifts and truncated signed division/remainder. The physical
output rules prove signed fit and sign-extend reads. Native parameters, captures,
loops, persistent locations and external results use the same semantic values;
external result buffers require sign-extended 32-bit representations.

The signed checkpoint passed the complete normal suite in
`build/relational-signed-regression.log`, including three new signed groups.
`make relational-signed-msx` passed 1,024 host and 773 Z80 activations; evidence is
`build/relational-signed-msx/run-bfe475dt/comparison.json`. Sanitizer evidence from
earlier checkpoints does not cover these signed changes.

Fixed-point and IEEE floating-point relationships are now supplied by a numeric
adapter. It shares the core's terms, constraints and delayed builders. Explicit
casts retain both source and destination types; fixed multiplication/division
use full intermediates and truncation, while floating values retain IEEE bits.
Finite domains retain their element type, so a typed floating candidate is not
reinterpreted as an integer literal. Native numeric calls, captures, persistent
storage and outputs pass host checks. Discarded nonfinite-to-integer casts keep
their trap obligation. Sorted floating iteration rejects NaN and uses numeric
order. Byte casts, aggregate operators and broader interfaces still need work.

Constant signed/unsigned integers now support widths through the 65,536-bit
construction budget. Wider runtime operations retain an explicit target
representation obligation; the current native C interface remains at most
32 bits. Half/single/double IEEE constants and wide fixed-point constants can
resolve without allocating target storage. Native half/single runtime arithmetic
is checked against independently rounded host calculations, including randomly
selected bit patterns and exceptional values.

`build/relational-wide-numeric-regression.log` records a complete normal pass,
including four signed and four numeric groups. Fresh target evidence is
`build/relational-numeric-msx/run-wy5mcxc9/comparison.json` (536 host activations,
532 valid Z80 activations and nonfinite cast trap 6) and
`build/relational-signed-msx/run-0d342bu8/comparison.json` (1,024 host and 773 Z80
activations). Numeric helper requirements are recorded in the source plan and
select the existing banked fixed/float modules in the editable project.

That numeric/wide-integer checkpoint passed AddressSanitizer,
UndefinedBehaviorSanitizer and LeakSanitizer on an isolated, frozen source
snapshot. The log, source manifest and checkpoint record are
`build/relational-numeric-sanitize.log`,
`build/relational-numeric-sanitize-manifest.json` and
`build/relational-numeric-sanitize.json`. This evidence predates the subsequent
aggregate comparison and representation-cast changes.


Aggregate equality and lexical ordering now use common element interfaces,
including unequal outer array extents, nested records, signed values and IEEE
NaN behavior. Explicit casts include element-wise numeric array copies,
canonical little-endian bytes without native padding, Boolean bit packing,
signed padding, wide constant representations and validation of decoded
schemas. Discarded invalid Boolean decodes still trap. Proven bit ranges remove
redundant validation and preserve the identity of a split/reassembled unsigned
value; this also prevents repeated C expressions from overwhelming SDCC.

The representation checkpoint passed the full normal suite in
`build/relational-representation-regression.log`. The subsequent simplified
projection passed 1,024 host and 770 valid Z80 activations plus Boolean trap 16:
`build/relational-representation-msx/run-rdh4u40g/comparison.json`. This target
result predates the latest type-domain changes and must not be reused as their
execution evidence. The first target attempt timed out in SDCC; it was not a
passing check.


Cyclic type relationships now retain finite domains of justified interfaces
instead of monotonically widening one candidate vector. Domain closure runs
ordinary relationships in isolated proposal requests. The closed domains,
original equations and initializer constraints enter the owning request
without selecting a type there. Nonmonotone overload cycles can therefore
backtrack, and an ordinary consumer or demanded value can eliminate an
alternative. Multiple type assignments may still establish one common
observation. Seedless general signature discovery remains open.

The complete normal suite passes in `build/relational-domain-regression.log`:
23,753 core assertions and 17 type-closure groups, including a separate finite
mapping oracle and all six search orders. Fresh target checks pass in
`build/relational-types-msx/run-ef1fp9s9/comparison.json` (768 host and 768 Z80
activations) and
`build/relational-representation-msx/run-6a3w4t8m/comparison.json` (1,024 host,
770 valid Z80 and invalid Boolean trap 16). The type workload now includes a
consumer constraining a nonmonotone type cycle. That checkpoint also passed AddressSanitizer, UndefinedBehaviorSanitizer and
leak checks on an isolated frozen snapshot. Its log, manifest and checkpoint
record are `build/relational-domains-sanitize.log`,
`build/relational-domains-sanitize-manifest.json` and
`build/relational-domains-sanitize.json`. These checks predate tuple changes.

Tuples now use ordinary record relationships, including independent field
properties, recursive generic record requirements, canonical byte casts,
overload patterns and native call frames. Destructuring evaluates its producer
once, then assigns destinations in order. Swaps preserve snapshots; repeated
destinations observe the last write. Output-only captures and early returns
use the same initialization and completion relationships as other assignments.
Record identity also projects independently through wrapper helpers, without
closing unrelated fields of a cyclic schema.

The complete tuple checkpoint passes in `build/relational-tuples-regression.log`
(23,753 core assertions, four tuple groups and five numeric groups). Fixed-point
width spelling is decimal, and oversized unused annotations do not exhaust an
unrelated demand. `build/relational-tuples-msx/run-xtyd9zqv/comparison.json`
records 1,024 host and 1,024 Z80 activations using the frozen executable
`build/relational-tuples-verified`. This checkpoint also passed AddressSanitizer,
UndefinedBehaviorSanitizer and leak checks on a frozen snapshot, recorded in
`build/relational-tuples-sanitize.json` with its adjacent log and manifest.
Receiver method changes are later work and are not covered by it.

Receiver methods now designate a logical root and path once before value
arguments. Overlapping views share ordered writes, including a whole record
and one of its fields. Private temporaries may convert to an annotated receiver
type; existing locations retain their type. Nested helpers capture the shared
root. Native method frames transfer each root once and carry dynamic indices
as ordinary scalar parameters, including output-only roots and recursive calls.
The complete suite passes in `build/relational-methods-regression.log`, including
four method groups. `build/relational-methods-msx/run-3l625448/comparison.json`
records 1,024 host and 1,024 Z80 activations. Operator/iteration dispatch and
non-evaluating probes still require their next implementation work.

Member and method probes now establish signature presence without evaluating
receivers, arguments or selected bodies. Overload matching uses independent
category and nominal-identity facts and recursively checks tuple requirements;
unrelated candidate schemas remain undemanded. Interface-property construction
also detaches activation-effect collection, so an isolated type proof can
replace a bundle with an assumption without leaving execution obligations
behind. Cyclic type-domain tests retain their backtracking behavior.

The full suite passes in `build/relational-probes-regression.log`, including
17 type-closure, four method and three probe groups. Refreshed method target
evidence is `build/relational-methods-msx/run-f_k4e2gk/comparison.json` (1,024
host and 1,024 Z80 activations). This is the checkpoint in the frozen executable
`build/relational-probes-verified`. AddressSanitizer, UndefinedBehaviorSanitizer
and leak checks passed on an isolated snapshot whose 205 input fingerprints
were rechecked after execution. Its evidence is
`build/relational-probes-sanitize.json`, with the adjacent log and manifest.
Refreshed type and boundary target checks are recorded in
`build/relational-types-msx/run-0heuhu0z/comparison.json` (768 host and Z80)
and `build/relational-boundary-msx/run-y8svch8q/comparison.json` (256 each).

Scalar unary, binary and compound operator methods now share ordinary receiver
dispatch and execution. Logical methods evaluate their arguments eagerly;
closed absence of an applicable overload preserves builtin short-circuiting.
Compound places are designated once, including dynamic indices, and the
method's result is stored after its receiver and capture writes.
`build/relational-operator-methods-regression.log` records the full passing
suite with three new operator-method groups, including native execution under
six search orders. Its frozen executable is
`build/relational-operator-methods-verified`. This checkpoint postdates the
sanitizer evidence above. Broadcasting, iteration methods, optional blocks and
the remaining milestones are ongoing work.

Broadcasting now aligns trailing dimensions, expands singleton dimensions and
lifts scalar operators over private element receivers. Both operands complete
once before the ordered element calls. Explicit dotted methods take precedence;
dotted compounds designate the target once and store after all element effects.
Empty results derive an element interface without executing a sample call.
Array/scalar broadcasts expose their outer length without closing record fields.
Inline calls return writes to visible logical locations, and native capture
analysis includes implicit operator calls and scalar lifting.

`build/relational-broadcast-closure-regression.log` passes the full suite, with
five broadcast groups, three operator-method groups and 23,753 core assertions.
`build/relational-broadcast-verified` preserves that executable. The preceding
target check passed 1,024 host and 1,024 Z80 activations in
`build/relational-broadcast-msx/run-qvcyjg3a/comparison.json`; it predates the
capture-closure and partial-category fixes. The refreshed target check passes in
`build/relational-broadcast-msx/run-gwkx12rc/comparison.json` with the same
activation counts. AddressSanitizer, UndefinedBehaviorSanitizer and leak checks
also pass on an isolated snapshot with 210 verified input fingerprints;
`build/relational-broadcast-sanitize.json` records it with the adjacent log and
manifest. Conservative native capture discovery and general partial array-shape
interfaces still need the later closure/planning work.

Custom iteration methods now own separate completion targets for the supplied
body. Continue resumes the method after that invocation; break escapes its
implementation loops; return reaches the caller. Forwarding retains those
targets, and each invocation receives a readonly snapshot of the method's `_`.
Nested uses of one method are distinguished by call site. Runtime control
premises can narrow a joined value back to its proven branch, allowing an
unrolled implementation counter to remain known after a conditional caller exit.
Explicit unresolved evidence is retained ahead of mechanical dependencies in
the bounded diagnostic summary.

`build/relational-iteration-methods-regression.log` passes all groups, including
four custom-iteration groups and 23,771 core assertions. Its executable is
`build/relational-iteration-verified`. The first combined target workload passed
its host oracle but failed megalinker's resident RAM/stack check:
`build/relational-iteration-msx/run-ey3mawsh` is failed evidence. It exposed the
cost of widening every loop-carried value to 32 bits. A typed storage projection
now retains each scalar leaf's representation in the residual loop interface.
The C projection uses separate typed fields with parallel updates and volatile
reads of the corresponding width. Signed and numeric loop families retain
their existing value semantics.

`build/relational-typed-loop-regression.log` passes the full suite. The same
combined target workload now passes 1,024 host and 1,024 Z80 activations in
`build/relational-iteration-msx/run-qxrsj7xi/comparison.json`. Generated resident
code fell from 16,011 to 7,019 bytes, and the run observed 83 stack bytes in the
16 KiB configuration. This is workload evidence, not a whole-program stack
bound. The frozen executable is `build/relational-typed-loop-verified`.
The latest sanitizer evidence above predates iteration and typed loop storage.

Unrolled repeats now preserve runtime break/continue guards, including continue
from a predicate, and restore the enclosing loop target on every completion
path. `build/relational-unrolled-guards-regression.log` passes the full suite
with five iteration-method groups. The updated iteration workload passes 1,024
host and 1,024 Z80 activations in
`build/relational-iteration-msx/run-zlbfsk9j/comparison.json`.
Its executable is `build/relational-unrolled-guards-verified`.

Sorted iteration now uses ordinary scalar comparison dispatch for methods and
aggregate values. Comparisons protect enclosing logical roots against writes,
including writes through aliases and nested helpers; scratch state and private
receiver copies remain mutable. Only the ordering must be compile-time known,
so a record can retain runtime payload fields. Private comparison receiver
storage is released from the caller's state after the invocation.

Four focused sorting groups pass under all six search orders in
`build/relational-sorted-focused.log`. The independent target workload passes
512 host and 512 Z80 activations in
`build/relational-sorted-msx/run-ehgf84wn/comparison.json`.
The full suite passes in `build/relational-sorted-regression.log`, with 23,771
core assertions. `build/relational-sorted-verified` matches the target evidence.
AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer also pass on an
isolated snapshot with 217 verified input fingerprints. Its evidence is
`build/relational-sorted-sanitize.json`, with the adjacent log and manifest.
This checkpoint predates resource loading and scoped module changes.

File and directory resources now supply binary-safe immutable input facts.
Type and shape queries avoid reading file contents or executing process imports.
Explicitly enabled processes run once per input revision, with output and time
budgets; repeated demands reuse the recorded stdout. Resource contents,
directory membership, missing preferred paths, process results and source
metadata are retained in the source report. Packaging checks the observed file,
directory and path-selection facts before producing a target project.

`build/relational-resources-regression.log` passes the full suite with three
resource groups. The shared path snapshot change also passes the established
module and input-closure tests in
`build/relational-resources-parser-regression.log`. The resource workload passes
512 host and 512 Z80 activations in
`build/relational-resources-msx/run-6fe4eajo/comparison.json`.
Its executable is `build/relational-resources-verified`. Scoped import
instantiation, bundled math and optional transactions remain ongoing work.

Scoped imports now close their declarations before use, including cyclic module
graphs and forward type relationships. A module instance is keyed by its lexical
import scope, so repeated calls and different function specializations share its
persistent state. Different importing blocks retain separate state. Module
environments expose visible declarations without capturing an importing
activation's automatic variables, including indirectly through nested helpers.
Directly declared nested functions keep ordinary lexical captures. Visible
function overloads accumulate across scopes.

`build/relational-modules-regression.log` passes the complete suite with four
module groups and four resource groups. Packaging now also rejects retargeted
symbolic links for resources and the entry source. The shared input-service
changes pass the established module/input-closure suite in
`build/relational-module-paths-parser-regression.log`. The target workload passes
256 host and 256 Z80 activations, and verifies exactly two persistent module
initializations, in
`build/relational-modules-msx/run-124to1o2/comparison.json`.
Its executable is `build/relational-modules-verified`. The sanitizer evidence
above predates these resource/module changes. Bundled math, optional transactions
and the remaining closure/backend milestones are still open.

Bundled math now supplies all seventeen operations through ordinary numeric and
shape relationships, including integer square root, integer-exponent power,
fixed-point rounding and IEEE predicates/payload handling. Empty broadcasts
derive interfaces without executing sample values. Project-local replacements
retain ordinary function semantics. Discarded square-root/remainder trap
obligations survive execution projection. Both the new six-group math suite and
the established ten-group suite pass; the latter verifies the shared pure
numeric service without linking the established solver into this core.

The combined math target exposed two realization problems. Serializing a
multi-byte leaf duplicated its residual expression; the emitter now snapshots
that leaf once. Keeping all generated code resident still exceeded RAM, so the
projection now also emits separate window B modules for source entries,
materialized functions and power helpers. Resident wrappers save and restore
the caller's bank. Recursive calls and calls through numeric/boundary helpers
use the same protocol. Host C remains available as a single translation unit;
the editable target Makefile selects the split projection.

`build/relational-banks-regression.log` passes the complete suite, including
23,771 core assertions, six math groups and two split-projection groups.
`build/relational-banks-verified` matches the executable used for the following
target evidence:

- Math: `build/relational-math-msx/run-_xxmot7a/comparison.json`, 1,024 host
  and 1,024 Z80 activations. It uses 7,392 resident bytes and observes 236 stack
  bytes; the 1,024-byte stack reservation remains unproven.
- Recursion: `build/relational-execution-msx/run-ul251r1u/comparison.json`,
  2,304 host and 54 Z80 inputs.
- Receivers: `build/relational-methods-msx/run-g4xg5dvt/comparison.json`,
  1,024 host and 1,024 Z80 activations.
- Boundary state: `build/relational-boundary-msx/run-_twalhvl/comparison.json`,
  256 host and 256 Z80 activations.
- Editable modules: `build/relational-banks-msx/run-jlxe03ei/comparison.json`,
  768 Z80 activations across generated C, handwritten C and assembly, with
  caller-bank restoration checked on every activation.

Initial math attempts failed linking, RAM placement, or the emulator tick
budget and are not passing evidence. Per-function bank cuts do not yet split
a single function larger than one window. AddressSanitizer, UndefinedBehaviorSanitizer
and leak checks pass on an isolated snapshot with 191 verified input fingerprints:
`build/relational-banks-sanitize.json`, with the adjacent log and manifest. The
first run stopped at a missing packaging-documentation fixture; after restoring
that fixture the complete suite passed. This checkpoint predates shape changes.

Array dimensions now have an independent property whose leaf carries no element
schema. Two-array broadcast length queries, trailing rank alignment, indexed
array categories, forward aliases and record-field shapes can resolve without
closing unrelated record fields. Explicit dotted methods keep their own result
shape. Schema captures retain the demanded shape facts; opaque proof interfaces
remain unknown and cannot be assumed scalar. Value execution still validates
the complete operand interfaces.

`build/relational-shape-regression.log` passes the complete suite with two new
shape groups. `build/relational-shapes-verified` matches
`build/relational-schema-msx/run-pfseemro/comparison.json`: 1,536 host activations,
1,024 valid Z80 activations and two expected invariant traps. General shape
inference through unresolved control and transforming type equations remains
part of the broader closure work.

Materialized calls now retain known value-argument leaves as specialization
facts. Only runtime leaves cross the native ABI. This supports constant strings,
wide integer constants, constant dimensions and mixed records alongside runtime
arguments, including recursion with a fixed string. Different static Boolean
arguments can establish different result interfaces without losing those facts
at the call boundary. Captured and returned non-native leaves still require
their next integration work.

`build/relational-specialization-regression.log` passes the complete suite,
including two new specialization groups. The frozen executable
`build/relational-specialization-verified` matches
`build/relational-specialization-msx/run-o5b9fxfd/comparison.json`, with
1,024 host and 1,024 Z80 activations.


Native call frames now carry known non-native return and capture leaves as
logical facts. Only native numeric leaves occupy ABI slots. Output-only and
updated string captures, mixed record receivers, wide constants and recursive
mixed tuples follow the same relationships. Aggregate conditional joins resolve
field by field; a common string fact can close independently of a recursive
numeric value. Runtime selection between different strings remains invalid
under the RFC's compile-time string semantics.

`build/relational-frame-facts-regression.log` passes the complete suite, including
four new frame-interface groups. The frozen executable
`build/relational-frame-facts-verified` matches
`build/relational-frame-facts-msx/run-jd10_r1b/comparison.json`, with 1,024 host
and 1,024 Z80 activations. The first target test had two equally specific visible
helpers; correcting their names removed that test-program ambiguity. AddressSanitizer, UndefinedBehaviorSanitizer and leak checks pass on an isolated
snapshot with 200 verified input fingerprints, recorded in
`build/relational-frame-facts-sanitize.json` with its adjacent log and manifest.
This sanitizer checkpoint predates aggregate source inputs. Lazy native
capture discovery and the remaining milestones are still open.


Source activation now accepts nested arrays, records and tuples through the
ordinary annotation relationships. The C ABI flattens their native leaves in
logical order; the plan reports each parameter's type, field path, range and C
argument. Empty inputs occupy no ABI slots. Partial field and nominal-identity
queries avoid unrelated schemas. Entry checks validate even unused parameters
before the body can invoke a sink. The four-source and 64-byte prototype caps
are removed; output placement remains constrained by its payload budget and
the target address space.

`build/relational-inputs-regression.log` passes the complete suite, including
four new boundary-input groups. The frozen executable
`build/relational-inputs-verified` matches
`build/relational-inputs-msx/run-du_8coa8/comparison.json`: 1,280 host and 1,280
valid Z80 activations across five sources, plus schema trap 19 and range trap
16. Initial focused checks exposed missing structural input copies and eager
nominal identity; both are corrected. The first target driver incorrectly used
the success-only simulation command for an expected trap; the corrected driver
records and checks trap ROMs directly. The sanitizer evidence above predates
these changes.


The closed execution graph now supplies a per-function local-storage policy.
`--frame-ram-bytes N` gives the target a static RAM budget; zero remains the
editable default. Recursive functions, potentially overlapping activations and
ancestors of open external/persistent boundaries retain automatic locals.
An exclusive child of a recursive function may still use static locals. A
largest-buffer-first policy selects a feasible subset under the supplied budget.
The report records the actual call edges, eligibility reasons and projected
local bytes; it explicitly leaves the stack bound unknown. Linked RAM checks
remain mandatory. Module overrides or edits to generated/runtime code invalidate
this graph proof when static locals are selected.

`build/relational-frame-plan-regression.log` passes the complete suite, with two
new frame-planning groups. `build/relational-frame-plan-verified` matches
`build/relational-frame-plan-msx/run-204e2yqm/comparison.json`: 512 host and 512
Z80 activations across automatic and exclusive policies, plus rejection of edits
to an automatic callee while its caller depends on the closed graph. The 128-byte
budget selects 24 bytes of static locals; this workload observes 326 versus
314 stack bytes. These measurements are not stack bounds. AddressSanitizer, UndefinedBehaviorSanitizer and leak checks pass on an isolated
snapshot covering source inputs and this policy, with 208 verified input hashes.
Its evidence is `build/relational-frame-plan-sanitize.json`, with its adjacent
log and manifest. Boundary-contract changes below postdate this check.


External resource footprints and activation feedback are now separate supplied
facts. `--resource-read SYMBOL:RESOURCE` and `--resource-write SYMBOL:RESOURCE`
declare a complete named footprint; the unspecified external world remains a
wildcard that can conflict with those resources. `--sink-no-feedback SYMBOL`
closes activation feedback without removing the external effect. Such a sink
can participate in exclusive local storage, while finite pure solving still
rejects its execution. `--access-order FIRST:SECOND` orders execution completion
before the next source starts, without releasing the earlier result. Cycles
are contradictory; transitive ordering closes shared-state hazards. Overlap
contracts no longer invent a start order from source-list position.

External calls now flatten nested arrays and tuples through the existing leaf
ABI, including empty arguments and array results. Unsupported native leaves
retain explicit target obligations. Resource and feedback assumptions are
recorded in the editable project; changing those contracts invalidates static
frame proofs. General feedback occurrences and outstanding-activation bounds
still need the broader activation model.

`build/relational-contracts-regression.log` passes the complete suite, including
four new contract groups. `build/relational-contracts-verified` matches
`build/relational-contracts-msx/run-n7oy_4zs/comparison.json`: 1,536 host and 1,536
Z80 activations across serial/static and ordered/retained-result configurations.
The initial focused failure used an invalid tuple-array annotation in the test;
its RFC-supported type-query spelling passes. Sanitizer evidence above predates
these contract and external-array changes.

## Loop initialization closure checkpoint

Loop storage now has an independent finite initialization relation. A Boolean
least fixed point follows entry, predicate-continue and body back edges. Each
read is a safety obligation under its branch and completion premises; ordinary
exits determine what is definitely initialized afterward. Fixed nonempty
iterables contribute their guaranteed first entry. Unchanged reaching values
supply invariants; tests of changing loop values are existentially forgotten
between iterations. Nested proofs publish their parent obligations before the
outgoing state, so builder scheduling cannot omit an inner read.

Lazy loop-slot interfaces admit first writes without closing unused lexical
declarations. Native aggregate values and readiness bits use parallel physical
updates. Zero-filled backing bytes are guarded implementation storage, never
initialization evidence. Guarded source reads, short-circuit operands and
compound assignments retain their required proof. This work exposed and fixed
two previously uncovered issues: missing branch premises and a compound-update
shortcut that bypassed initialization when fetching an existing reaching value.
Numeric induction beyond these Boolean invariants remains open.

The full normal suite passes in `build/relational-loop-init-regression.log`,
including five new groups under all six search orderings. The target driver
passed 3,072 host and 3,072 Z80 activations with automatic and exclusive local
storage. Evidence: `build/relational-loop-init-msx/run-z9bmqqu0/comparison.json`;
the executable matches `build/relational-loop-init-verified`. The frozen snapshot also passed AddressSanitizer, UndefinedBehaviorSanitizer
and leak checks: `build/relational-loop-init-sanitize.json` records 214
reverified input hashes. It covers the preceding boundary contracts as well,
and predates the subsequent function-interface work. The eight-milestone
objective remains incomplete.

## Shared facts and realization checkpoint (2026-11-18)

The default resolves the imported program together, including pure value
bindings and ordered execution regions. Argument/result/capture bounds, branch
premises and native boundary lifetimes feed the shared value graph before
representation and workspace selection. Exact structural term identities
preserve DAG sharing; cached answers retain their owner, assumptions and input
revision. Immutable zero/one terms are shared within their factory. Bounded
proof-table growth reduces rehashing without changing proof keys or budgets.

Finite intermediate domains can become constants, comparisons, bit tests,
affine operations or bounded ROM tables. These realizations rejoin the value
graph. Native widths guide arithmetic helpers, sparse products and transport.
Known operands pass through wrapping conversions into native immediates. Zero
products remove value work while preserving preceding effects. Case interfaces
support invariant outputs and independent guards under one input snapshot and
activation. Fixed workspace reuse still requires nonoverlap evidence.

Estimates propose implementations. Measured search retains validated
nondominated alternatives and rechecks their fingerprints. The Edelweiss driver
records common inputs, tools, relevant environment settings and exact physical
preferences before baseline reuse. Cost policy cannot weaken source semantics,
obligation completeness or behavioral validation.

The final compiler passes all 91 release groups and the same frozen ASan/UBSan
suite, all 55 examples and 38 supported host/Z80 drivers. The known-open
`type_equations.vx` value remains intentional; its type query resolves. Focused
target checks cover signed/unsigned conversions, branch budgets zero/eight,
automatic/fixed frames and effects whose entire result becomes zero.

All three Edelweiss Linux/MSX profiles were rebuilt and checked during the run.
The final zero-product change reproduces all 494 generated game code files and
the measured optional player projection exactly, so the verified binaries and
scenario evidence are reused. Six worklist/branch orderings preserve all checked
game traces in both player implementations. Reversing the imports also passes
host traces, an MSX encounter, callback, bank and RAM checks. Private generated
names may differ across schedules; semantic order independence does not require
identical C files.

The selected production game occupies 470,986 ROM bytes and 3,912 global RAM
bytes, with 42 fixed Vexel workspace bytes. The encounter observes 119 stack bytes. Active melee gains speed with
four additional callback stack bytes. The optional local player region now
observes 17 stack bytes, but remains slower than the selected 26-byte path;
the faster default stays selected. Measured maxima are not worst-case bounds.

Complete same-source generation took 240.28 seconds with the recorded early-run
compiler and 24.37 seconds with the final compiler (9.86 times faster). Peak
solver RSS was 3,452,328 versus 1,263,188 KiB. Both use the same recorded boundary
offers and pre-bomb-policy options; the generated
implementations intentionally differ. This measures solver generation, not
SDCC compilation or game frame rate.

A second comparison ran the final compiler first, then the early-run compiler,
with the same recorded inputs. The early-run compiler took 272.01 seconds and
the final compiler 23.70 seconds (11.48 times faster), with 3,452,508 versus
1,263,432 KiB peak RSS. Both complete measurements are retained; elapsed time
varies between runs.

An isolated instrumented build of the final source reproduces all 484 raw
compiler-generated C/header/include files for the benchmark. Its profile now
concentrates on Boolean memo operations and binding traversal. In particular,
`Session::conditionedValue` rebuilds a proof context for each ready scalar query.
Measuring repeated contexts is the next investigation target; any answer reuse
must retain exact premises, native-expansion dependencies, observation identity
and revision. This is a profiling lead, not an implemented cache or a release
timing claim. The profile and findings are under
`tmp/eight-hour-20261118/closure/profile-final/`.

The implementation is concentrated in these components:

| Component | Responsibility |
| --- | --- |
| `experimental/relational/src/machine.cpp`, `term_index.cpp` | Shared terms, exact identities, bounded diagnostic rendering and local traversal reuse |
| `experimental/relational/src/source_control_proof.cpp` | Compact Boolean proof storage and elimination of undemanded proof work |
| `experimental/relational/src/source_value.cpp`, `source_value_tables.inc`, `source_value_choices.inc` | Native arithmetic, finite realizations, branch facts and shared value scheduling |
| `backends/msx-neo8/tools/optimize.py`, `ext/edelweiss/tools/optimize_player.py` | Validated measured alternatives, input identity and artifact checks |

The complete evidence is in
`ext/edelweiss/build/eight-hour-closure/validation.json`, with compiler, input,
test-log, generated-code and binary fingerprints. The preceding fresh game
build is recorded in `ext/edelweiss/build/eight-hour-delivery/validation.json`.
The run ledger is `tmp/eight-hour-20261118/run.json`. Changes are uncommitted.

SDCC remains serialized with 2 GiB and 180-second limits per invocation and
`--max-allocs-per-node 2000`. Selected modules fit their 8 KiB banks. Edelweiss
retains its KonamiSCC mapper; separate backend examples exercise NEO8.
Sanitizers use `detect_leaks=0`, so this is ASan/UBSan coverage, not a fresh
leak-check result. General invariant discovery, compiled solver acceleration,
an LLM proposal service and resolved Vexel serialization remain future work.

## Scoped proof reuse and execution facts (2026-11-19)

Conditional answers now retain exact ordered premises and revalidate every
native interpretation used by the proof. Bounded Boolean graphs share pure
subexpressions while each query rebuilds its own assumptions. A graph filled by
earlier queries gets one fresh-graph retry. Failed or exhausted answers never
become cached facts. Native proofs retain their constructed relationships but
solve again with fresh bindings; occasional larger attempts avoid repeatedly
spending the same small allowance on a difficult request.

Bounded modular algebra supplies equality, constant and immutable-alias facts
to the same function, capture and loop relationships. These are proof forms;
the backend does not emit expanded polynomials. Conditional-constant comparisons
can avoid constructing irrelevant bit-level predicates. Exact structural atom
identities, complemented Boolean handles and local DAG traversals reduce proof
storage and repeated work without changing observation or assumption scopes.
Loop initialization preserves sharing during normalization, dependency discovery
and changing-value classification. Source-leaf previews explicitly mark
truncation, and the arithmetic C renderer avoids a duplicate traversal of its
left operand while retaining identical C. A final proof-table change packs the
bounded handles before hashing; full triples still determine equality on every
collision. Three paired measurements of that isolated change gave medians of
9.21 and 8.90 seconds with identical whole-game reports, including proof counters.

Execution has a separate finite completion/effect certificate. Invocation demand
can remove an unused pure call while another caller still needs the same body.
All actual evaluations and logical return/capture leaves participate; loops,
unanchored recursion, foreign effects and possible traps keep their obligations.
The ordinary, value and region projections consume the same certificate before
frame and bank planning. [The execution-facts example](../examples/execution_facts.vx)
shows one removable call, an observable sink and a potentially trapping call
whose execution must be preserved.

Editable user C, assembly, headers and stack contracts now participate in each
adaptive MSX build and its input identity. A changed embedding invalidates
earlier size/resource feedback. Output inside its own input directory is rejected
before work begins. Assembly stack analysis recognizes joins whose paths restore
the same known stack address, while retaining each path's earlier peak; unknown
restorations and unsupported transfers remain unproved.

The final paired whole-game benchmark uses one CPU, the same inputs and options,
two starting-compiler runs and four final-compiler runs. Median generation time
falls from 17.17 to 9.26 seconds (46.1%), and median peak RSS from 1,264,142 to
576,780 KiB (54.4%). Every run reproduces all 484 raw generated code files.
These measurements cover solver generation; they do not measure SDCC compilation
or imply a change in game frame rate. They use this run's starting compiler and
must not be combined with the preceding checkpoint's different benchmark options.

The accepted source passes all 95 release groups and all 95 ASan/UBSan groups,
57 example checks and 39 supported host/Z80 drivers. The `type_equations.vx`
value is still intentionally open; its selective type query passes. Sanitizers
use `detect_leaks=0`. An independent arithmetic/capture/effect/trap oracle checks
128 programs across all 256 byte inputs, using fatal UBSan on generated C.
Both Edelweiss host variants pass, including all six worklist/branch orderings
and reversal of all 28 imports. Different schedules can change private names;
their values and effect traces agree.

The normal Make release executable is byte-identical to the separately tested
executable after removing debug/symbol information and the GNU build-id note.
The sanitizer tests use an identical source copy; sanitizer diagnostic records
embed its different filename. All 119 executable sections of the changed
sanitizer object match, but linked sanitizer byte identity is not claimed.
Both compiler fingerprints and the source overlay are retained in the report.

All 494 postprocessed game code files remain identical to the selected production
tree. The earlier Linux/MSX machine evidence is reused after checking the game
inputs and all 863 retained machine artifacts; this checkpoint does not claim a
fresh game ROM build. Edelweiss still uses KonamiSCC. Separate NEO8 measurements
exercise the new unused-call certificates:

| Workload | Frame allowance | Harness-inclusive Z80 ticks, before → after | Observed stack, before → after |
| --- | ---: | ---: | ---: |
| Identity chain | 0 bytes | 745,111 → 520,087 | 36 → 24 bytes |
| Identity chain | 128 bytes | 714,987 → 489,963 | 32 → 22 bytes |
| One unused call | 0 bytes | 1,016,073 → 784,649 | 44 → 42 bytes |
| One unused call | 128 bytes | 974,513 → 750,215 | 36 → 36 bytes |

Those measurements cover all 256 inputs. The final compiler reproduces the
measured projects' compile/link inputs and semantic plans; their ROM hashes are
checked again. These are synthetic workloads, and observed stack maxima are not
worst-case bounds.

The bank-pressure example now obtains external responses during its recurrence.
This retains actual dynamic work after stronger value proofs showed that its
previous pure recurrence was constant. The fixture exercises measured partition
retries and requires an assembly stack bound; it does not disable the optimizer
to preserve an obsolete stress case.

The fresh bank-pressure run rejects a 9,020-byte continuation and retries with
a smaller partition allowance. Its four final continuations occupy 4,376, 4,365,
2,981 and 4,361 bytes. The required stack bound is 565 bytes, including a 64-byte
environment allowance; the observed peak is 523 bytes. SDCC remains serialized,
with 2 GiB, 180-second and 2,000-allocation limits per invocation. An audit of
812 retained resource records, including rejected attempts, finds a maximum RSS
of 1,455,348 KiB. The record count can include copies retained by example drivers.

The complete checkpoint is recorded in
`ext/edelweiss/build/eight-hour-20261119/validation.json`, with a copy at
`tmp/eight-hour-20261119/hash-delivery-report.json`; the run ledger is
`tmp/eight-hour-20261119/run.json`. Changes remain uncommitted. The game's tracked
`sdcc_msx` symlink was observed as a directory during this run, changing its status
from modified to a type change. Its origin was not established and it was left
untouched; the observed vendor files are fingerprinted separately. The checked
game inputs and retained artifacts still match.

Shared solver terms do not yet imply shared arithmetic in every C projection.
The ordinary emitter can still duplicate a demanded loop-transfer expression.
Typed value and region emitters already materialize selected common values;
extending that mechanism requires coordinated temporary lifetimes and frame/bank
accounting. General invariant discovery, compiled solver acceleration, LLM
proposal guidance and resolved-Vexel serialization also remain future work.

An isolated profile quantifies that remaining gap without changing emitted code.
Across 672 ordinary expression renderings in Edelweiss, 6,327 operation visits
represent 4,343 distinct operations summed per root. The largest expression is
17,847 bytes and visits 41 operations 569 times. Its player-weapon path, and the
other largest fairy/melee paths, already use optimized value bindings in the
game; these generation observations are not runtime-hotness measurements.

A separate shared-arithmetic experiment compares the existing ordinary and
region realizations of identical Vexel. At four repeated square/add steps, the
ordinary path takes 12,696,930 harness-inclusive Z80 ticks and observes 54 stack
bytes; the region path takes 1,043,264 ticks and observes 26 stack bytes with
six bytes of fixed binding workspace. Both agree on every byte input.
This compares complete realizations, including typed arithmetic and storage;
it does not isolate the contribution of common-expression sharing. At twelve
steps the ordinary source unit grows to 738,929 bytes, while the region header
is 4,405 bytes plus 913 bytes of type declarations and 14 workspace bytes.
The latter passes all 256 host inputs under fatal UBSan; that large ordinary
unit was not sent to SDCC. Evidence is under
`tmp/eight-hour-20261119/hash-shared-expression-regions/`. The corresponding
ordinary-expression profile is under `tmp/eight-hour-20261119/hash-expression-profile/`.

The next generalization should share the existing typed scheduling machinery
across projections. A reusable calculation needs both its value identity and an
evaluation interval: the guard under which it executes, the snapshot versions it
reads, and the effects or transfers that end its validity. Candidate storage must
then participate in activation interference, frame budgets and bank planning.
The experiment supports that direction; automatic use of region scheduling by
every ordinary source interface is not implemented in this checkpoint.

## Ordinary calculation intervals (2026-11-20)

The shared-calculation experiment above now has an ordinary-C implementation.
The typed graph, use counting and shared-node scheduling in `source_value.cpp`
serve ordinary expressions as well as value/region bindings. Ordinary C places
sequenced assignments inside the original expression, preserving lazy guards,
readiness, effects and checked operations. Each retained node has a typed
scratch lane. Separate argument expressions use disjoint lanes, while completed
statements can reuse storage. Projection caches retain their term roots and are
local to one physical function in one solve.

A pre-emission pass accounts for scratch before activation/frame selection.
The final emitter checks that it stays within that closed storage/helper plan.
Source entries, native frames, loop transfers, call arguments and resident lazy
initializers participate. Bank continuations capture the same storage, and
helper definitions follow each embedding's `VX_RESIDENT` contract. No new syntax
or opt-in annotation is required. Unsupported or possibly trapping operations
keep the ordinary checked path; children can still form smaller intervals.

The paired square/add experiment uses identical source and all 256 byte inputs:

| Realization | C source unit | Z80 entry code | Ticks including harness | Observed stack | Fixed locals |
| --- | ---: | ---: | ---: | ---: | ---: |
| Previous ordinary, 4 updates | 4,401 B | 851 B | 12,668,262 | 54 B | 8 B |
| Scheduled ordinary, 4 updates | 3,215 B | 226 B | 1,284,733 | 30 B | 11 B |
| Scheduled ordinary, 12 updates | 6,374 B | 356 B | 2,496,801 | 30 B | 19 B |

The matching previous 12-update ordinary source is 738,801 bytes; it was not
sent to SDCC. The improvement combines retained calculations with the existing
typed arithmetic emitter, and does not isolate CSE or imply a game-frame-rate
increase. Fixed locals include the original loop/result storage; the new scratch
accounts for 3 and 11 bytes respectively. These stack numbers are observations
of these workloads, not universal bounds. All generated bank modules pass their
actual machine-code placement checks; C text size is not ROM section size.
Evidence is in `tmp/shared-scheduling-20261120/benchmark.json` and the associated
`bench-final-*` projects.

Edelweiss was regenerated in one whole-program solve. Its ordinary paths retain
14 intervals across `contact_box`, `ghost_update` and `title_step`, using 11
scratch bytes in total. These entries already have typed game bindings, so this
is primarily a general backend improvement. The fresh Linux executable and
475,136-byte KonamiSCC ROM are under
`ext/edelweiss/build/shared-scheduling-20261120/vexel/`. C/Vexel gameplay oracle,
resource, linker and optimization-manifest checks pass. This checkpoint does not
claim fresh full-engine emulator timing or a gamewide speedup.

`make calculation-msx` reproduces the host/Z80 checks for guarded arithmetic,
effectful result snapshots, lazy initializers, recursion, automatic/fixed locals
and forced bank continuations. A separate host matrix checks different source
ABI widths in a single program. The existing value/region paths retain their
own storage policies and continue using the same shared-node machinery. General
cross-guard/effect retention, intra-expression register allocation and globally
optimal scheduling remain outside this bounded policy.

`tools/publish_repositories.py` previews the intended changes in both repositories
and performs commits/pushes only with `--apply`. Vexel currently needs a supplied
remote URL. The script preserves the observed `sdcc_msx` directory/symlink
mismatch, includes actual changes under `vendor/sdcc_msx`, and excludes unrelated
loose files. Disposable local-repository tests cover preview, publication,
reruns, staged-index rejection and remote-ahead rejection. No working-repository
commits or pushes were performed during implementation.

Final validation: 97 release groups and 97 ASan/UBSan groups passed
(`ASAN_OPTIONS=detect_leaks=0`), along with 57 example checks and all 39 supported
host/Z80 drivers. The `type_equations.vx` partial value remains a recorded open
obligation; its selective type check passes. The six focused calculation tests
also pass with `--msx` (the source-ABI matrix is host-only). All 494 generated
game code files reproduce exactly in a fresh solve. A single paired game
generation under concurrent sanitizer load took 12.78/13.02 seconds and peaked
at 575,988/576,544 KiB before/after; this is descriptive evidence, not a
statistical performance claim. SDCC stayed serialized with 2,048 MiB and
180-second limits; the largest observed completed-job RSS was 673.9 MiB.

The final evidence bundle is `tmp/shared-scheduling-20261120/validation.json`,
with a copy at `ext/edelweiss/build/shared-scheduling-20261120/validation.json`.
It fingerprints the compiler, implementation, generated code, fresh Linux/MSX
artifacts and validation logs. The final release compiler SHA-256 is
`c18940bdd59af1311eb04a11502abb97704abe8b6bf4f7c99e367618bdc477de`.

## Scratch lifetimes across C realizations (2026-11-20)

The common typed scheduler now assigns storage by last use within each emitted
calculation block. It counts complete consumers rather than textual references,
recycles slots only after the entire store has finished, and requires matching
width and signedness. This covers ordinary comma expressions, pure value
bindings and region expression scratch. Logical retained-value counts stay
separate from physical slot counts, so frame budgets reflect actual storage.

A fixed-output prefix receives the roots of later guards and branch outputs.
These roots keep its producers live until their later consumers. Inherited
locals remain owned by their enclosing scope; branch-local declarations are not
shared across C lexical scopes. Finite lookups follow their physical index
rather than assuming semantic node order. Parallel-transfer cycle storage,
callback snapshots, activation proofs and unspecified C argument order retain
their existing constraints. This is a conservative allocator within the existing
scheduler, without a new intermediate language or source annotation.

The bounded SDCC/Z80 square/add fixture checks all 256 byte inputs against an
independent oracle. The paired twelve-update measurements are:

| Storage policy | Scratch before / after | Declared locals before / after | Observed stack before / after | Entry code before / after | Whole harness ticks before / after |
| --- | ---: | ---: | ---: | ---: | ---: |
| Exclusive fixed | 11 / 2 B | 19 / 10 B | 30 / 30 B | 356 / 356 B | 2,496,801 / 2,496,612 |
| Automatic | 11 / 2 B | 19 / 10 B | 47 / 38 B | 294 / 294 B | 2,487,698 / 2,487,698 |

The automatic invocation's assembly-derived stack bound also falls from 28 to
19 bytes; its scope excludes embedding frames. Fixed RAM initialization accounts
for the small fixed-policy timing difference. The entry instructions retain the
same size; this change saves storage without claiming faster arithmetic. The
four-update fixed fixture also uses two scratch bytes (previously three).

In Edelweiss, `player_view` shrinks from 16 to 15 bytes and `scene_camera` from
18 to 16 bytes. The sum of selected frames before overlay falls from 664 to
661 bytes, while their actual shared workspace remains 42 bytes: another live
frame still determines that allocation. Eleven of the 494 generated code files
change, and a second complete generation reproduces all 494 files exactly.

The fresh game build uses 471,084 ROM bytes in the unchanged 475,136-byte image
and 3,912 bytes of global RAM. In the paired smoke builds, used ROM rises from
304,535 to 304,551 bytes, with the same 311,296-byte image and 3,932-byte global
RAM allocation. SDCC reduces the player module by 38 bytes and grows the
camera-transition module by 54. Its camera helper also reserves two local spill
bytes instead of one. Smaller declared frames are therefore not a guarantee of
better register allocation; these measurements remain relevant to future
realization selection.

Both complete SDL/openMSX runs pass the quest, dialogue, scene and restoration
checks. The comparison uses frozen pre-change Vexel output versus the new Vexel
output, alongside the separate C/reference gameplay checks on the host. Observed
extra stack remains 242 bytes in both runs. The measured ghost callback remains
2,139.9 microseconds active and 602.4 microseconds skipped, including the
trampoline and ISR; no game-wide speedup is claimed. The main game remains on
its existing KonamiSCC mapper.

Validation passed: 97 release groups, 97 ASan/UBSan groups with leak detection
disabled, all 39 supported host/Z80 example drivers, and all 57 example checks.
The existing `type_equations.vx` partial-value obligation remains explicitly
open while its selective type check passes. The seven focused lifetime tests,
the full value-projection target suite and the region target suite pass. The
focused test's whole-project deadline permits waiting for the serialized SDCC
queue; individual jobs remain limited to 2,048 MiB and 180 seconds. Among 1,457
retained job records, the largest RSS was 673.6 MiB. Fresh game builds, gameplay
oracles, resource/linker checks and deterministic regeneration also pass.

Evidence is recorded in `tmp/scratch-lifetimes-20261120/validation.json` and
`ext/edelweiss/build/scratch-lifetimes-20261120/validation.json`. The release
compiler SHA-256 is
`3eabd1c212fe885ddee836466a9ab48030d5c04d2f1dc5d054ee377439b57a8f`.
Changes remain uncommitted in both repositories; the existing publication
script continues to collect the source, documentation and generated changes.

## Eight-hour optimization session, 2026-11-20

This session extends the existing solver/projector boundary in three connected
ways. First, proved integer domains and consumer bit demands survive native
arguments, captures, results and loop snapshots. Second, the projector can use
those facts to choose smaller arithmetic and storage representations. Third,
linked target measurements can reject a physical choice and request a bounded
alternative without changing source meaning. The goal remains a closed source
solution with reviewable, replaceable C realizations.

Calculation intervals now share a common integer range evaluator with completion
checks. The proof is about converted operands: a nonzero source number can become
zero in a smaller representation, and a valid mathematical shift count can become
invalid after conversion. Checks that can still trap remain observable even when
the result is discarded. A result's own completion certificate permits sharing
only within the already certified pure region; callback order, snapshots,
parallel assignments and activation overlap retain their existing constraints.

Physical representation facts are carried across closed native interfaces.
They are facts about the transported values, not new restrictions on the public
source inputs. The independent formal-argument fixture drops from 13,418,404 to
2,092,510 target ticks and from 88 to 58 observed stack bytes; its fixed-frame
variant drops from 83 to 53 bytes. A loop-carried fixture falls from 38,960,754 to
5,138,715 ticks and from 77 to 53 bytes. These are complete test-harness costs,
not estimates of application frame time. The exact comparisons are retained in
`tmp/eight-hour-20261120-0838/transport-domains-comparison.json`.

Proved native-width division and shifts no longer fall back to generic checked
32-bit operations merely because no temporary needs materializing. Constant
non-power-of-two divisors use width-specific arithmetic; bounded variable
divisors use compact loops that preserve the high remainder carry. The unsigned
byte division-by-seven fixture falls from 25,998,425 to 1,342,735 target ticks
and from 71 to 38 observed stack bytes. Signed cases include minimum/-1 wrapping,
negative remainder, negative divisors and all supported 8/16/32-bit widths.
These results do not justify unrolling every expression: 32 different constant
divisors in one source expression produced a 29,520-byte function.

That oversized function exercises a new measured feedback path. The backend
identifies its owning function and retries with
`calculation_division_unroll.OWNER=0`, before reducing statement partitions.
The compact realization fits in 2,311 bytes and passes the independent target
oracle in exactly two attempts, with unchanged statement partitions. Unrelated
functions retain their original choices. Each allowance can be disabled once;
subsequent partition reduction is also bounded. Exhausted choices still produce
a failure rather than claiming the original request was resolved. The actual
linked experiment is retained under
`tmp/eight-hour-20261120-0838/division-bank-closed`.

Quotient/remainder sharing is a separate opt-in alternative. It replaces a
proved pure same-width pair with `q=x/d; r=x-q*d` and retains the original
remainder range. For unsigned 32-bit division by seven, sharing saves roughly
96,000 harness ticks with automatic storage but grows observed stack from 54 to
58 bytes. Fixed storage saves roughly 101,000 ticks with the same 46-byte stack.
The artifact-validated selector therefore rejects sharing for the automatic
candidate and selects it for the fixed candidate under a stack-first objective.
The feature defaults off; a smaller expression is not enough evidence to make
it the universal choice. Edelweiss currently has no matching selected pair.

Target execution also exposed two SDCC-specific correctness hazards. Wrapped
unsigned byte shifts need explicit high-bit correction; a directly enclosing
mask may safely absorb that correction. A stored remainder combined with
another byte operand could compile as `r & r` instead of `d & r`. Dynamic byte
bitwise operations crossing stored-calculation boundaries now use the same
private register-argument barriers as the affected arithmetic operators. The
failing mixed graph and assembly are retained, and independent mixed signed/
unsigned host and Z80 checks cover the correction. Helper demand now follows
individual published code units, so an embedding can place those units in
separate translation units without accidentally relying on another file's
static helper definitions.

This work does not claim globally optimal arithmetic, a universal SDCC stack
model, exhaustive gameplay coverage, or closure of every proposed RFC feature.
The existing partial-value observation in `examples/type_equations.vx` remains
explicitly open; its selective type observation still resolves. Measurement
costs describe their recorded binary, workload and storage choice. Production
gameplay continues to use its existing KonamiSCC mapper and hand-written engine
and assembly boundaries.

Unsigned-byte rotations provide another concrete realization choice. The
initial private-call form saved instructions but increased automatic stack;
composing one-bit steps eliminated SDCC's unnecessary masks and spills. Explicit
inline expansion then removed the remaining call overhead. The final seven-count,
256-input fixture falls from 5,396,766 to 5,092,772 target ticks with automatic
storage and keeps the same 118-byte observed stack. Fixed storage falls from
4,481,660 to 4,243,464 ticks with the same 48-byte stack. A second fixture checks
derived/stored producers and materialized shift children, so recognition cannot
extend an expired scratch lifetime. Rotation helpers remain opt-in because an
expansion's effect on enclosing register allocation is still a physical choice.

Edelweiss selects that allowance. Its weapon-selection binding falls from
130.16 to 85.69 microseconds and from 6 to 2 observed helper stack bytes relative
to the correct bitwise-barrier realization. Every measured input improves.
Compared with the earlier shift-corrected code, the mean falls from 107.34 to
85.69 microseconds with the same 2 bytes and no slower sample. All 57 full
bindings across 63 linked instances pass the oracle; the other bindings remain
unchanged by this choice. The production generation reproduces the measured
candidate's 517 C/header/fragment files exactly.

A separate reciprocal-expression experiment illustrates why algebraic proof
alone is insufficient for selection. Byte division by seven can be expressed
as `((n+1)*73)>>9` in a sufficiently wide carrier, and all 256 inputs verify.
Emitting its shift/add form through Vexel nevertheless changes the automatic
ordinary harness from 1,342,735 ticks / 38 stack bytes to 1,374,073 / 42; fixed
storage changes from 1,298,615 / 30 to 1,324,363 / 32. The value-binding alternative
also loses. This experiment was rejected and does not alter the compiler or
game source. Its independent oracle and target artifacts remain available under
`reciprocal-seven-baseline` and `reciprocal-seven-probe` in the session directory.

Public result serialization now participates in the same storage accounting.
The original projection retained a separate 32-bit snapshot for every
multi-byte result. It can instead use the solved payload width and reuse one
snapshot after each result has been completely serialized. The expression is
still evaluated once at its original arithmetic width. Signed interpretation,
checks, effect order and activation eligibility are unchanged.
`calculation_publication_narrow` and `calculation_publication_reuse`, including
physical-function scopes, expose all four alternatives. Reports include the
effective carrier width, number of values requiring snapshots, and allocated
snapshot bytes. Both allowances default to one and intersect their scopes.

The word-arithmetic fixture gives the following measured choices. The workload
and independent oracle are identical; ticks include the complete harness with
ordinary and unchanged typed-binding calls, not just serialization instructions.
All fixed-storage candidates observe the same 52-byte stack.

| Carrier / slots | Snapshot bytes | Automatic ticks | Automatic stack | Fixed ticks |
| --- | ---: | ---: | ---: | ---: |
| 32-bit / separate | 16 | 616,170 | 96 B | 561,366 |
| 32-bit / shared | 4 | 616,170 | 84 B | 561,114 |
| 16-bit / separate | 8 | 599,786 | 88 B | 553,038 |
| 16-bit / shared | 2 | 601,834 | 82 B | 552,912 |

The artifact-validated selector chooses narrow/shared under a stack-first
objective. It retains narrow/separate on the automatic-storage frontier because
that alternative runs faster while using six more stack bytes. In the separate
byte-arithmetic fixture, sharing saves 12 automatic stack bytes with unchanged
ticks, but fixed sharing is 324 ticks slower over the complete workload. These
are measured alternatives, not a claim that less local RAM always means faster
instructions or a smaller target stack.

The new defaults reproduce 1,513 C/header/fragment files across all 54 emitted
example images, and all 517 game code files, from the independently validated
shared-slot implementation. The additional allowance changes the selected
realization only when explicitly disabled. The complete four-choice matrix,
selected candidates and checked artifacts are retained as
`publication-realization-matrix.json` and `publication-selection-*.json` in the
session directory.

The selected game configuration mixes these choices. It retains wide, separate
publication carriers for `vx_password_decode` and uses the new defaults
elsewhere. The ordinary frame allowance is 256 bytes, with 201 planned static
bytes in the complete image; omitted ordinary alternatives do not become linked
RAM. The actual full game uses 3,900 global RAM bytes and 467,375 occupied ROM
bytes, compared with 3,912 and 468,688 in the preceding inline-rotation candidate.
The image remains 475,136 bytes. Canonical generation reproduces all 517
C/header/fragment files of this measured mixed candidate exactly.

An independent direct probe checks 256 encoder and 256 decoder inputs in the
actual linked game, including valid, lowercase, invalid and special passwords.
The encoder mean falls from 1,305.19 to 1,127.52 microseconds, with every measured
input improving and the same 24-byte observed stack. Narrow decoder carriers would improve its
mean but increase observed stack from 65 to 71 bytes; sharing at the previous
frame allowance increases that to 72. The selected decoder therefore retains
its previous fragment, timing and 65-byte stack. These scopes include entry
through return, exclude caller argument stores and result-reader calls, and
have interrupts disabled. The reusable game tool is `tests/profile_password.py`.

The complete game smoke scenario still observes 242 additional stack bytes.
The host oracle, fresh Linux/MSX builds, all 57 full bindings across 63 linked
instances, encounters, actor/world profiles, title flow and direct password
checks pass. The linked ordinary-frame inventory also shows that every linked
function with a nonzero planned local frame now uses fixed storage. Further
stack work needs to address argument/result transport and SDCC spills, with
separate lifetime proofs for any reusable private call frame.

The bounded storage search at `run-e1wl8gsp` retained its baseline binding
choices. Enabling the player-view output schedule increased mean time from
174.77 to 178.96 microseconds with unchanged stack; individual samples exceeded
the 2% slowdown allowance, so the candidate was rejected. The next candidate
exhausted the search wall budget during its smoke run. Five remaining candidates
were unmeasured. Input identity and retained artifact validation both pass.
Several earlier complete-project controllers reached their outer deadlines
while waiting in the serialized compiler queue; unfinished scenarios are not
counted as successful validation.

A final cache-assisted replay also measures the previously unfinished separate
scratch-slot alternative. Independent 256-input probes cover all 54 full
bindings across 60 linked instances in each smoke image. Both alternatives
preserve values. The output schedule repeats its 174.77 to 178.96 microsecond
regression with unchanged 2-byte stack. Disabling player-view scratch reuse
increases its frame from 15 to 16 bytes, mean time from 174.77 to 234.36
microseconds, and observed helper stack from 2 to 5 bytes. The other measured
instances retain their costs. Both alternatives fail the per-input time/stack
limits, supporting the existing scoped choices. The full probe comparison is
retained as `final-storage-full-probe-comparison.json`.
Both candidates also complete the comparative SDL/MSX gameplay scenario. The
replay at `run-2l13vgpc` retains the checked baseline and closes with successful
input-identity and artifact validation; its summary is
`final-storage-search-summary.json`. This finishes the earlier interrupted
player-view comparison without treating either slower realization as an
optimization gain.

The C/reference engine still provides a useful remaining target. In this
scenario its ghost callback takes 1,707.87 microseconds when active and 415.19
when skipped, compared with 1,931.76 and 602.36 for Vexel. The reference scenario
observes 240 additional stack bytes versus Vexel's 242. Callback measurements
include the trampoline and ISR, so they do not attribute the whole difference
to the pure body or establish an application frame-rate difference. The session
improves the selected Vexel realization without claiming parity with every C
reference path.

The compiler guard now passes its lock descriptor into the child and installs
an inherited kernel CPU backstop. A forced supervisor termination cannot release
the gate while the compiler continues running. A controlled interruption test
checks this behavior, alongside memory/time failures and ordinary serialization.
Resource feedback recognizes a CPU-limit rejection through the same emitted
owner identities. Logs separately report queue waiting, active wall time, child
CPU consumption and peak RSS. A real tiny SDCC job illustrates the distinction:
it waited 55.327 seconds for the shared gate and then ran for 0.010 seconds.

The final compiler passes all 111 release and 111 ASan/UBSan command groups
(leak detection disabled), all 60 example checks and all 39 supported example
drivers. The known partial-value observation remains open as described above.
The complete 26-realization width suite passes on host and Z80; a fresh final
compiler generation reproduces all 678 C/header/fragment files in that target
suite. Focused supervision, feedback and cache-identity checks supplement these
runs. The final compiler SHA-256 is
`6c87cb0a9c9d48d351906e3ca398e0b139947eed08009882458524d72f8291ce`.

One final experiment separates payload encoding from snapshot storage. The
isolated password encoder can use a 10-byte, byte-element payload instead of a
20-byte, word-element payload. Both host and Z80 runs check the same 256 vectors
and all ten public result-reader calls per vector. Complete harness ticks fall
from 4,660,017 to 3,525,445, with the same 77-byte observed stack; planned static
locals fall from four to two bytes. This is an isolated NEO-8 experiment, not a
whole-game speedup. Tightening the game's common arena to 14 bytes is infeasible
because its mixed-width player and world outputs still require 20. A measured
per-source encoding preference remains future work. The experiment is retained
as `password-encoding-comparison.json` with its input, project and oracle hashes.

Reference compilation now has its own artifact-validated cache. Its identity
retains game sources, the canonical generated inputs, tools, platform resources
and environment, while excluding the Vexel solver and pure-helper probe vectors
that the C/reference build does not consume. A cold search spent 198.8 seconds
building the reference profile with 276 guarded SDCC jobs. A second search with
a different helper sample count reused that exact profile and issued no
reference compilation commands. Both searches passed their newly executed
comparative SDL/MSX scenarios, host checks and target probes, and both regenerated
the canonical 517 code files exactly. A changed `PATH` correctly produced a
cache miss; that diagnostic run was stopped and is not counted as a success.

The final resource audit records a maximum SDCC RSS of 674.3 MiB and a longest
active invocation of 105.634 seconds. Recorded compiler jobs retain the 2 GiB
address-space and 180-second wall limits; no memory, CPU or wall-limit exhaustion
was recorded. Outer search deadlines and superseded compiler failures are
separately retained in the session ledger. Final artifact fingerprints and
validation counts are in `tmp/eight-hour-20261120-0838/validation.json`, with a
copy under `ext/edelweiss/build/eight-hour-20261120-0838/validation.json`.
The implementation and generated game remain uncommitted for the publication
script; that script still previews by default.
