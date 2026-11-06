# Four-pass acceptance contract

This is a finite acceptance contract, not a promise to decide arbitrary pure
programs or prove every invariant. An unfinished relationship must remain
`unresolved` or `exhausted`; a missing target representation cannot invent a value.

## 1. Language and configuration closure

| Acceptance case | Executable evidence |
|---|---|
| Copies, nested places, aggregate arguments, joins, captured writes and output-only captures | `test_aggregates.py`, `test_tuples.py`, `test_capture_closure.py`, `test_function_types.py` |
| Partial properties, aliases, cyclic equations, dependent record invariants and validation | `test_type_closure.py`, `test_schema_invariants.py`, `test_shapes.py` |
| Recursive interfaces, changing native captures and fixed-layout back edges | `test_recursive_captures.py`, `test_capture_modes.py` |
| Return/break/continue through arguments, expression parameters, loops, joins and predicates | `test_completion.py`, `test_expressions.py`, `test_iteration_methods.py`, `test_loop_initialization.py` |
| Numeric families, representation casts, methods, broadcasting, resources and imports | Family suites in `experimental/relational/Makefile` |
| Source/import/CLI configuration, declaration permutations, conflicts and target constraints refining source choices | `test_configuration_streams.py` |
| Computed input bounds, address bindings and stream capacities | `test_configuration_streams.py`, `test_inputs.py` |
| Finite choices, uniqueness, order independence and Sudoku | `test_finite.py`, `oracle_sudoku.py` |

Configuration is not evaluated before the physical puzzle. An ordinary choice
of capacity 1 or 2 resolves to 2 when a requested 16-bit runtime result requires
it. The chosen value is still checked as a source observation. Preferences
cannot choose between distinct source meanings.

## 2. Boundary lifetimes

Finite occurrence graphs remain supported. Repeated streams add a serial
execution gate, bounded result pools, explicit leases, arbitrary release order,
backpressure and non-reused token identities. Different pools cannot release
each other's results. Nested submission cannot violate exclusion. Host cases
exercise saturation, recycling, retained results, stale reads and sink feedback;
`closure_msx.py` exercises the same ABI after SDCC and linking. Adapters must
serialize concurrent calls into a pool; concurrency remains a boundary contract.

## 3. MSX realization

| Acceptance case | Executable evidence |
|---|---|
| 16 KiB resident RAM without assumed mapper RAM | Baseline in `closure_msx.py` |
| Mapped ownership, capacity rejection and segment crossings | `test_backend_closure.py`, mapper profile in `closure_msx.py` |
| Bank cuts preserve loops, transfers, calls and recursion | `test_backend_closure.py`, both target profiles |
| Measured object sizes feed smaller bank-cut attempts | `realize_msx.py`, `bank_pressure_msx.py`; rejected sizes, selected cuts and execution across every continuation bank |
| Stack joins, ABI frames and unsupported SP behavior | `test_backend_closure.py` |
| Actual assembly bounds, guarded recursion, explicit external assumptions and required-proof failure | `tools/stack_usage.py`, `tools/check_project.py`, negative checks in `closure_msx.py`; reports bind evidence to ROM/object hashes |
| Editable C/assembly preserves or invalidates frame assumptions explicitly | Existing bank/frame/layout suites and project Makefile |

An indivisible operation may exceed a bank; an application may exceed RAM; an
adapter may lack a stack contract. These are unsatisfied target obligations,
with diagnostics and no successful realization. Finite hardware cannot realize
every valid source program.

## 4. Consolidation

`make` builds the single supported frontend. `make test-closure` runs the new
focused checks. `make closure-msx` creates editable projects and compares host
and Z80 execution with a separate arithmetic oracle. `make bank-pressure-msx`
tests oversized functions through measured retries and Z80 execution. `make msx-project
SOURCE=... OUTPUT=...` exports/builds with measured bank feedback. The normal
regression and sanitizer targets include the new cases. The RFC, architecture,
source contract and backend guide describe this path; historical commands live
in a labeled archive. The completion ledger records validation evidence.

Optional future work includes finer reuse inside a request, compiled solver
acceleration, LLM search advice and resolved-Vexel serialization. These cannot
replace unresolved obligations or waive the acceptance checks.
