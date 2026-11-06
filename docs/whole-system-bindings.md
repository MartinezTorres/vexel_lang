# Declarations, persistent state, and physical bindings

The solver distinguishes discovering a declaration from executing an access.
This matters for order independence: finding a module late must not change
which declarations an earlier expression can use, while two runtime writes
still need a definite execution order.

The parser records source modules and import sites without registering every
discovered declaration in one global namespace. Module discovery reads and
parses source files, including source reached through scoped imports. Binding
then establishes the declaration set for the importing lexical scope before
building that scope's expressions. Repeated and cyclic imports contribute one
copy of a declaration to that scope. Syntax errors remain errors even in an
optional block; a missing import has a separate resolution diagnostic that an
optional block may discard.

This arrangement keeps declaration visibility independent of the worklist
schedule. A function call before an import sees the same overload candidates
as a function call after it. Scoped immutable constants are separate graph
obligations, so forward references resolve through dependencies. An unused
constant does not demand its initializer.

## The lifetime of a module instance

An importing scope is a lexical source location, not an invocation frame.
Calling a function twice must not create two copies of its imported persistent
state, and specializing that function for a different known argument must not
create another copy either. The graph therefore gives module storage an
identity derived from its lexical import context and declaration identity.
Different importing scopes have different identities.

Automatic variables still belong to activations. Imported declarations cannot
borrow an importing activation's automatic variables or expression parameters.
Their environment contains declarations. A nested function written directly
inside a block continues to capture that block's automatic variables normally.
This distinction prevents a module's initializer from accidentally retaining
the first invocation's parameter value.

Module-binding maps belong to the reversible graph revision. A rejected
structural assumption or optional block cannot leak a newly created storage
identity into the accepted result. Immutable file and process snapshots have
their existing shared input lifetime; persistent runtime storage is not a
compile-time process side effect.

## A value equation and a storage location are different obligations

An effect-free, known global initializer establishes an immutable value. A typed
initializer that remains runtime establishes a persistent location and an
initializer obligation. A typed declaration without an initializer establishes
a location with no initial value. The surface syntax remains keywordless.

A type query asks for the location's type without performing a read. A value
read creates an explicit `GlobalRead` obligation with an execution predecessor.
A write uses the ordinary store relation with a persistent destination. Both
retain runtime effects. The solver does not substitute a previous known write
for a later read across an arbitrary external callback or invocation.

This is deliberately conservative optimization. A future analysis could prove
that a location is private, that no intervening operation can change it, and
that a particular read equals a previous store. Such an optimization must
carry those access assumptions. Treating the location itself as an immutable
constant would erase the distinction the proof depends on.

Non-constant initialization occurs once on first access. This gives independent
declarations no incidental source-order execution. If initializer A reads B,
that read demands B's initialization first. A runtime cycle or callback into an
initializer already in progress traps. An unused location executes nothing.
An assignment evaluates its right-hand side before performing its persistent
write, including initialization required by that write.

## Physical realization on the MSX

The language location has no numeric address and no universal pointer type.
The MSX realization chooses resident C storage for ordinary globals, with an
initialization flag where necessary. A non-constant initializer is an editable
banked module. Its resident guard selects the initializer's bank, calls it,
restores the previous bank, stores the result, and marks initialization complete.
The guard traps on reentry. This also gives the linker concrete sections to
measure against the 16 KiB RAM contract.

External globals map to volatile C symbols supplied by the editable project.
Explicit backend bindings can name a CPU memory address or an 8-bit I/O port.
Those are separate physical spaces in the generated plan. C addresses and
SDCC port declarations belong to the realization, while source records, arrays,
receiver locations and copies retain their language semantics.

An explicit address is a caller-supplied hardware contract. It does not prove
that arbitrary memory is free or that a particular cartridge bank is selected.
The current binding implementation checks address range, size and port type.
Allocation of additional RAM-mapper segments, automatic access scheduling, and
proof of ownership remain separate target-planning requirements.

## Evidence and limits

The tests exercise three worklist schedules for source rules, host-C calls for
persistent values across separate entries, and linked Z80 execution for bank
restoration, aggregate initialization, shared and separate module instances,
memory bindings, and initialization traps. Host address/port variables are
explicit shadows for testing; they do not emulate devices.

This implementation closes the basic declaration/storage distinction. It does
not complete the RFC. Dependent field dimensions, loop-completion type proofs,
and logical/dotted operator dispatch now have executable coverage. The bundled
math module and general cyclic type equations still require work.
The joint solver's larger physical search and proof tasks
remain listed in the implementation ledger.
