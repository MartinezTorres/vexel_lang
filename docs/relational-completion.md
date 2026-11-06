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
