# Capability and interface revision

This revision implements three deliberate language restrictions. It preserves
keywordless syntax, partial/dependent types, whole-program relationships,
compile-time execution, finite backtracking, source/sink boundaries and the
pointerless model. It does not claim that the entire experimental RFC or MSX
roadmap is complete.

## Capability guards

Replace a semantic transaction:

```vexel
// Removed syntax:
// #{ device.write(bytes); };
```

with a signature test and ordinary control flow:

```vexel
device.?write(bytes) ? { device.write(bytes); };
```

For a free function use `?write(bytes)`. Existing member probes retain the form
`device.?member`. All three are compile-time, non-evaluating observations.
`?write(1/0)` can be true if its structural argument interface matches. Actually
calling `write(1/0)` must still meet the normal arithmetic obligations.

A true capability does not promise that the selected function body is valid,
that a call terminates, or that it needs no initialization. Those obligations
belong to the call. An unresolved or ambiguous signature cannot produce false.
There is no body-error fallback to a less-specific overload.

An absent capability skips its branch through ordinary conditional semantics.
Its constraints never become active. A present capability activates those
constraints normally; an inconsistent requirement is a contradiction. There
is no rollback of writes, declarations, finite choices, imports or effects.
`#{ ... }` is a parse error with a migration message, even in an unused function.
The former transaction implementation has been deleted in both source adapters.

Optional services should be represented by available signatures, commonly in a
project-local platform module. Imports remain static declarations in their
owning scopes. This revision does not add exception handling for failed imports.

## Recursive result and state interfaces

Simple anchored recursion still infers its result:

```vexel
&~count(k:#u8) { k==0 ? (#u16)0 : count(k-1)+1 }
```

An unanchored result cycle needs an annotation or a grounded relationship.
`->#u16` supplies a type; it does not establish termination or fabricate the
result of compile-time execution. The JSON `interface_inputs` field reports
outstanding recursive result/capture interfaces with source locations.

Automatic capture inference can still infer read-only, output-only, read/write
or type-only dependencies. At recursive capture cycles the solver checks one
conservative interface. It no longer enumerates omitted roots and possible
access modes until a proposal succeeds. This can retain state mentioned in an
unselected overload. If that state cannot supply its required interface, use
explicit state or establish the missing type/initialization in the source.

```vexel
&~count(k:#u8, state:#u16)->#u16 {
    k==0 ? state : count(k-1,state+1)
}
&^entry(n:#u8) { count(n,11) }
```

This example needs no hidden capture. At a recursive edge, the changing native
constant `state` becomes a runtime argument. An invariant constant can stay
static, including when it determines a fixed array extent. Known-input calls
still execute at compile time. Runtime recursive parameter types, shapes and
non-native static facts must remain fixed; changing these asks for an explicit
fixed runtime representation instead of generating endless specializations.

Multiple returned state values use ordinary tuples or records:

```vexel
&~fill(k:#u8)->(#u16,#u16) {
    k==0 ? ((#u16)0,(#u16)17)
         : { part=fill(k-1); (part.__0+1,part.__1+1) }
}
```

These are logical values. A backend may choose its own calling convention and
storage without exposing a source pointer or unified address space.

## Closed lexical declaration membership

Each lexical scope publishes an immutable declaration table after collecting
its statically named imports. Every member participates regardless of textual
order. Overload specificity is decided against that set, while applicability
may still wait for argument/type evidence. Types and values remain refinable.

An import inside a function body belongs to that body's scope. Calling the
function or querying it cannot inject a new overload into the caller. Scoped
module instances, import cycles and whole-module `std` overrides remain.
Source/import edits create a new input revision; they do not amend an old table.
`SourceProgram` starts a new snapshot; `--then` changes demands within it.
`SourceWorkspace` and `--session` now reuse closed requests whose full input
support still matches, creating a fresh `SourceProgram` for affected groups.
This is coarse reuse across revisions, without changing declaration membership
inside a revision. See the [session contract](../experimental/relational/SOURCE.md#incremental-sessions).

## Build and compatibility

`make`, `make test` and `make sanitize` now select the relational frontend.
`build/vexel` and `build/relational/vexel-source` are the same executable. The
older implementation is retained as historical source, with no automatic
fallback. Its old command-line flags and test suite are not the supported API.
The relational implementation's directory name remains to avoid an unrelated
repository-wide move.

Use `--query type:name`, `--query value:name` or `--query length:name` for
observations, and `--source name --emit-c directory` for a C implementation.
`make test-msx` builds editable SDCC + megalinker projects and checks the revised
capability and explicit-state examples on the host and simulated Z80.

The semantic-transaction tests were replaced by capability tests and parser
migration errors. Arithmetic, initialization, capture and trap tests now use
ordinary blocks. Tests that previously required recursive proposal enumeration
now exercise explicit interfaces and the deliberate rejection boundary. The
core's finite solver and its independent mathematical oracles remain.

The revision passes normal regression coverage and the complete ASan/UBSan
suite. The full Sudoku agrees under six worklist/branch-order combinations
without guessing. Generated MSX projects pass 3,072 host and 3,072 simulated Z80
activations. The [verification ledger](relational-completion.md) records the
artifacts and distinguishes this completed revision from the wider RFC roadmap.
