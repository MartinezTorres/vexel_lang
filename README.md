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
make test
make sanitize SANITIZE_LOG=/tmp/vexel-sanitize.log
make test-msx
make test-incremental
make incremental-msx
make test-closure
make closure-msx
make bank-pressure-msx
make msx-project SOURCE=examples/closure.vx OUTPUT=build/my-project
```

Queries and plans are JSON. `--query type:name`, `--query value:name` and
`--query length:name` request independent properties. `--source name` requests
a physical source activation; `--emit-c` writes an inspectable implementation
into a new or empty directory. The [source contract](experimental/relational/SOURCE.md)
describes boundary graphs, memory budgets, input tracking and target packaging.

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
