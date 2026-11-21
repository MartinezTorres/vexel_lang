# Vexel

Vexel resolves a program as a puzzle of information dependencies. Types, values,
shapes, execution and target constraints contribute relationships. Known values
become constants; declared runtime inputs leave executable operations. Unknown
information is never silently treated as a runtime value or a false capability.

The supported implementation is the C++20 relational solver in
[`experimental/relational`](experimental/relational/README.md). The directory name
is historical. `make` builds it as **`build/vexel`**; `make test` exercises this
implementation. It reuses source recognition and numeric/resource helpers from
`src`, without linking the earlier `Solver`. The earlier source and its `tests/`
remain reference material (some backend harnesses are still shared).

## Build and use

Requirements: Make, a C++20 compiler and Boost headers. Tests also use Python 3
and a C11 compiler. MSX project builds require SDCC and sdasz80; megalinker and
the simulation support are bundled in the backend.

```sh
make -j2
build/vexel examples/puzzle.vx
build/vexel examples/sudoku.vx --query value:solution --max-work 20000000
build/vexel examples/relational_capabilities.vx --source guarded --emit-c build/guarded
build/vexel examples/incremental_msx/game.vx --program --emit-c build/game-image
make test
make sanitize SANITIZE_LOG=/tmp/vexel-sanitize.log
make test-msx
make test-incremental
make incremental-msx
make test-closure
make closure-msx
make bank-pressure-msx
make publication-msx
make msx-project SOURCE=examples/closure.vx OUTPUT=build/my-project
```

Queries and plans are JSON. `--query type:name`, `--query value:name` and
`--query length:name` request independent properties. `--source name` requests
a physical source activation; `--emit-c` writes an inspectable implementation
into a new or empty directory. The [source contract](experimental/relational/SOURCE.md)
describes boundary graphs, memory budgets, input tracking and target packaging.

`--program` resolves all exported entries and build values in the imported
program together. Every C projection publishes a `program-image.json` catalog
with code-unit ownership, reconstructible fragments and artifact fingerprints.
Scheduling and callback lifetime contracts remain explicit. Target builds run
SDCC serially under memory/time limits; initial realization can retry smaller
chunks for the affected function when compilation or bank placement exceeds its
limits. Other functions keep their partition policies; measurements are tied to
the same source/resource revision.

Native value ranges also flow between callers, results and loop state before
transport is selected. A predicate proved constant from all its callers can
lose its result slot while retaining its producing call and checks. All C
projection variants share these facts; declared types remain unchanged.
Proved bounds can also shrink private arguments, results, captures and loop
cells to bytes or words while preserving the original arithmetic width and
public C interfaces. The plan reports their logical and storage types.
Each invocation can also query the shared function graph with its own argument
and capture bounds. This preserves useful return facts across wide and narrow
callers, and across successive state updates, without cloning functions or
memoizing runtime calls.
Branch queries also use their own guards when bounding results and loop
transfers. This can prove a small result from full-width inputs, while keeping
conditions scoped to their branch and preserving wrapping arithmetic.
Shared native inputs also combine the domains of their guarded invocation
paths. This can shrink helper arguments and captures across forwarding calls
while preserving unrestricted public entries and condition-side effects.
Equivalent value queries now share proof answers across result fields and
branch visits within the same scope and fact environment. The cache is local
to a solve; refinement rounds and changes to readiness hypotheses invalidate
its answers. Reports expose reuse and contextual work.

Optional control proofs also reuse completed answers and bounded Boolean
subgraphs within a source revision. Native interpretation dependencies are
revalidated before answer reuse; an unfinished proof can retain its construction
while retrying with fresh bindings. Bounded modular algebra proves wrapping
integer identities and shares equal values across call and loop interfaces.
Execution has a separate proof: an unused private invocation can disappear when
its body, callees and actual arguments are proved to complete without observable
effects. Traps and unproved completion remain part of the execution graph.

Closed pure integer graphs can also expose typed C value and case interfaces.
Cases carry their established facts across result fields, including independent
conditions, and publish only the inputs they still need. Finite intermediate
functions may become ROM tables, constants, bit tests or narrow arithmetic;
proofs preserve every integer conversion. Fixed workspace sharing uses explicit
activation contracts. Bounded measurement search retains validated speed,
stack and ROM tradeoffs alongside its selected realization; algebraic simplicity
alone is not treated as a target cost measurement.

Physical integer domains and demanded bits now survive closed native arguments,
captures, results and loop snapshots. The backend can use those facts for narrow
arithmetic, shared results and bounded per-function storage alternatives.
Measured bank failures can request compact division for the affected function
before repartitioning its code. Published units carry their private helper
support so an engine can place them in separate C modules. See the
[optimization session evidence](docs/relational-completion.md#eight-hour-optimization-session-2026-11-20)
for the measured gains, rejected alternatives and remaining limits.
Public result snapshots have independent width and reuse choices, including
per-function preferences. The four-choice `publication-msx` workload retains
editable SDCC projects and measured costs under `build/publication-msx`, so
less temporary RAM can be compared with actual stack use and execution time.

For live edits, run `build/vexel --session` and enter one request per line:

```text
solve examples/incremental_msx/game.vx --source frame
solve examples/incremental_msx/game.vx --source tone
solve examples/incremental_msx/game.vx --source frame
quit
```

An unchanged closed request is reused. Edit files between requests: changed
dependencies rebuild their groups, while unaffected groups remain cached.
`--session --verify-incremental` checks reused results against fresh solves.
The [session contract](experimental/relational/SOURCE.md#incremental-sessions)
describes the granularity and report fields.

## Revised language contract

Vexel remains keywordless, pure within an activation, and pointerless. It supports
partial structural types, dependent records, compile-time execution, finite
constraint solving with backtracking, runtime recursion and explicit source/sink
boundaries. Sudoku uses ordinary finite choices, arrays and constraints.

Three rules make this manageable:

- **Capabilities inspect signatures.** Use `?write(bytes)`,
  `device.?write(bytes)` or `device.?member` in an ordinary conditional. Probes
  execute neither arguments nor bodies. Selected bodies retain normal errors
  and constraints. The old transactional `#{ ... }` form is rejected.
- **Recursive interfaces must close.** Base paths can infer results; a return
  annotation anchors an otherwise open result. Capture inference uses one
  conservative fallback, without enumerating hypothetical capture subsets.
  Supply typed parameters/results when implicit state cannot close. Changing
  native constants become runtime parameters at recursive edges; invariant
  constants can still determine shapes.
- **Declaration membership is fixed per scope and input revision.** All its
  declarations and static imports participate regardless of textual order.
  Checking a callee cannot inject caller overloads. Types and values remain
  refinable; edited inputs start a new revision.

See the [RFC](docs/vexel-rfc.md), [architecture](docs/architecture.md) and
[migration guide](docs/language-simplification.md) for the precise rules.

## MSX NEO-8

The backend exports an editable SDCC + megalinker project with C, assembly
extension points, bank modules and inspectable placement facts. It targets the
16 KiB baseline RAM model and uses source/sink occurrence contracts for safe
frame sharing. `make test-msx` executes the revised capabilities and recursive
state examples on both the host and simulated Z80.

`make incremental-msx` replays source and resource edits, compares generated
artifacts with clean solves, and builds two editable NEO-8 projects. It records
ROM sections, resident RAM and observed stack use for a sprite-state and tone-data
workload. It does not emulate video or sound devices.

The [four-pass checklist](docs/closure-checklist.md) names the acceptance cases.
Source/import/CLI configuration now contributes common typed relationships;
bounded stream leases, mapped persistent storage, bank continuations and
conditional assembly stack bounds have executable support. `make closure-msx`
combines them in host and Z80 workloads. The [backend guide](docs/backend-msx-neo8.md)
describes editable projects, resource grants and proof assumptions.

The project remains experimental. Arbitrary invariant proving is not decidable;
open obligations and resource exhaustion stay explicit. Finer incremental reuse,
compiled solver acceleration, an LLM proposal service and resolved-Vexel
serialization are future work. The [completion ledger](docs/relational-completion.md)
records the verified checkpoint rather than treating those ideas as implemented.

## Publishing Vexel and Edelweiss

Preview the accumulated changes selected for both independent repositories:

```sh
python3 tools/publish_repositories.py
```

To publish the reviewed selection, run the script with `--apply`.

```sh
python3 tools/publish_repositories.py --apply
```

Vexel publishes to `relational-solver` by default. The script creates that
local branch from the current checkout when needed and pushes it to the
matching remote branch, preserving the existing GitHub `main` history.
Use `--vexel-branch NAME` to choose a different branch. If Vexel has no
configured `origin`, the script defaults to
`git@github.com:MartinezTorres/vexel_lang.git`. Edelweiss uses its own
`origin` and current branch.

The script checks both destination branches before staging. An existing
destination must be an ancestor of the local history, and pushes use Git's
normal fast-forward rules. The script stops for an existing staged selection
or unfinished Git operation. See `--help` for commit message, remote name and
URL options. It uses your configured Git identity and falls back to Manuel
Martinez <salutte@gmail.com>.

Tracked changes and new files in the source/documentation/generated-code
folders are selected. Loose files such as `mdl.jar`, screenshots and editor
workspaces are excluded. If a directory is mounted over Edelweiss's tracked
`sdcc_msx` symlink, the script preserves the tracked symlink; actual vendor
changes under `vendor/sdcc_msx` are included. No commits, remote configuration
or pushes occur in preview mode. Pushes across two repositories are not atomic;
if one fails, successful commits/pushes remain and a later run can finish.
