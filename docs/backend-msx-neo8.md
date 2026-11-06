# MSX NEO-8 realization

The supported path is the relational C++ solver, an editable C/assembly project,
SDCC Z80 ABI 0, and the bundled megalinker. The earlier frontend's `--emit-msx`,
`--msx-entry` and `__target_*` interface is historical, not this executable's API.

## Generate and inspect

```sh
make -j2
make msx-project SOURCE=examples/closure.vx OUTPUT=build/my-project
make -C build/my-project
make closure-msx
make bank-pressure-msx
```

`OUTPUT` must be new or empty. `VEXEL_OPTIONS='--source entry --budget 64'`
selects unannotated roots. To export without invoking SDCC, use:

```sh
python3 experimental/relational/realize_msx.py examples/closure.vx build/editable
```

The project contains `generated/*.c`, the source/target plans, resident runtime,
`user/main.c`, `user/stack-contracts.json`, `local.mk`, a Makefile, and pinned
megalinker/Z80 model sources. `user/vxp_NAME.c` or `.s` replaces that generated
module; assembly takes precedence. Initial export can retry measured oversized
bank modules with smaller continuation groups. It records every attempt in
`realization.json`. Rebuilding an exported project never replaces user edits.

## Configuration relationships

Annotations and CLI contributions use the same solver relationships. Their
numeric arguments are ordinary Vexel expressions, including imported values,
record properties, function calls and finite choices. Configuration is not
resolved by an independent evaluator before source constraints run.

```vexel
#Limits(payload:#u16, stack:#u16);
[[require(payload_bytes, limits.payload)]]
[[require(stack_bytes, limits.stack)]]
[[prefer(frame_ram_bytes, 128)]]
limits=#Limits(64,1024);
[[source]] &^frame(n:#u8){n+1}
```

| Relationship | Role | Default / supported range |
|---|---|---|
| `ram_bytes` | fact | 16384 in this resident profile |
| `rom_bank_bytes` | fact | 8192 |
| `payload_bytes` | requirement | 64 / 0..65535 |
| `stack_bytes` | requirement | 1024 / 32..12288 |
| `mapper_segments` | requirement | 0 / 0..255 owned extra segments |
| `call_depth` | requirement | 0 (unbounded) / 1..65535 guarded bank entries |
| `stack_proof` | requirement | 0 / 1 requires a closed assembly bound |
| `frame_ram_bytes` | preference | 0 / 0..12288 local-buffer allowance |
| `bank_chunk_operations` | preference | 128 / 1..4096 continuation operations |

CLI forms are `--fact KEY=N`, `--require KEY=N`, `--prefer KEY=N`.
`--budget N` and `--frame-ram-bytes N` contribute the corresponding relationships.
Repeated hard requirements conjoin: different explicit values conflict, including
two values from the CLI. Defaults apply only when nobody supplied that key.
Multiple allowances combine by their minimum, independent of origin/order.
Preferences choose a physical policy; they do not choose a source answer.
Unknown relationship keys are errors. Unknown ordinary metadata remains inert.
Reports retain contribution origins, including on contradictory requests.
Operational settings such as `--max-work`, output paths and scheduling stay
outside this vocabulary. These hardware facts describe this backend, not every
possible Vexel target.

## Memory and ownership

The BIOS-compatible profile retains page 0 and uses resident RAM starting at
`0xC000`, with stack top `0xF380`. Linked data, resident code, stream bookkeeping
and selected static locals must fit below `stack_top - stack_bytes`. There is
no assumed RAM below `0xC000`. ROM windows are 8 KiB; code wrappers preserve
window B across nested calls. The current export allows 256 ROM segments.
These choices follow the [NEO mapper specification](https://aoineko.org/msxgl/index.php?title=NEO_mapper).

`[[stream(capacity)]] &^frame(...)` creates a bounded pool for repeated events.
Capacity is a solver expression in 1..255. Generated APIs are:

- `vx_frame_submit(...)`: returns a nonzero lease, or zero for full-pool,
  execution-gate or token-space backpressure.
- `vx_frame_read_lease(lease,index)`: reads an owned scalar result leaf.
- `vx_frame_release(lease)`: relinquishes ownership, returning whether it did so.

Submission serializes execution across the generated pools. Results can remain
live together and can be released in any order. Nested submission from a sink
returns zero. The token space is shared by the projection and never wraps;
a stale token cannot silently become another result. Lease reads after release
trap with reason 21. The raw `vx_frame`/`vx_frame_read` functions are internal
scratch operations in this mode: adapters use the lease API. The pool is not
an atomic multi-thread API; an adapter for concurrent boundary events must
serialize access to it. Finite named occurrences and explicit overlap graphs
remain available for other activation contracts.

`[[mapper]]` attaches a placement requirement to an owned persistent global.
Constants can still be embedded without storage. Live mapped globals consume
native scalar bytes in a separate logical address space; their total must fit
`mapper_segments * 16384` in the source solve. Uninitialized and lazy-initialized
locations retain the ordinary readiness checks. No mapped address escapes into
Vexel, and mapper storage does not enlarge the resident C stack.

The embedding adapter must first call `vx_mapper_init(slot, first_segment,
count)` to grant exclusive ownership of a consecutive set of physical RAM
segments. This is an explicit grant, not an installed-RAM probe or DOS allocator.
A DOS adapter can reserve segments before making the grant. Accessors borrow
page 2, use port `0xFE`, and restore the cartridge slot and interrupt state.
The profile owns the RAM mapper's page-2 selection register; other adapters must
honor that ownership. See the mapper manufacturer's
[register specification](https://map.grauw.nl/resources/sound/musical-memory-mapper.pdf).
The baseline has no such grant. An uninitialized, repeated, or out-of-range grant
traps with reason 22. The integration example tests both the resident baseline
and extra mapper storage, including an access crossing a segment boundary.

`[[msx_address(expression)]]` and `[[msx_port(expression)]]` bindings also demand
ordinary source expressions. Absolute resident bindings are checked against
linked ownership and the stack reservation.

## Bank cuts and stack contracts

Small functions remain editable whole-function modules. Larger generated
functions become continuation groups with an explicit target frame and program
counter. Branches, loop back edges, break/continue and results cross those cuts;
source calls and recursive frames retain their ownership. Generated frame
headers make the carried state inspectable. Each group is a window-B module.
Their program counter and carried parameters add target bookkeeping beyond the
source local-buffer allowance; the final linked-RAM check includes these bytes.
The measured object sizes can cause initial realization to retry smaller groups.
An indivisible operation or a resident arena that cannot fit is a target failure,
not a silently truncated program. Groups are bounded by a 16-bit control-state
index. An operation count is a partition policy, not a claim about Z80 byte size.

`call_depth` adds a shared counter to generated bank wrappers. Exceeding the
explicit depth bound traps with reason 23. Zero retains unbounded source
recursion and cannot supply a finite recursive stack proof.

After SDCC, `tools/stack_usage.py` traverses assembly control flow, SP adjustments,
checked switch tables and the actual call graph. It handles balanced runtime
loops and depth-bounded generated recursion. Unknown SP changes, indirect calls,
unguarded recursion and missing external contracts leave the proof open. It
never substitutes observed stack use for a bound. Changing guard/trap code
invalidates the inherited recursion bound.

`user/stack-contracts.json` supplies `environment_bytes` for embedding/concurrent
boundary frames and a `functions` map of assembly symbols to maximum additional
stack bytes (excluding the return address). BIOS routines and handwritten
adapters need real contracts. These are explicit assumptions, recorded in the
report. The bound covers each named source entry; it does not prove arbitrary
user code's stack use. A bound exceeding the reserve rejects the realization.
`[[require(stack_proof,1)]]` also rejects an open proof.

The generated `resolution.json` binds placement, stack results, assumptions,
input hashes and object hashes to the actual ROM. `simulate` checks that ROM
and records conditional workload evidence. Its Z80 core runs real linked code;
BIOS calls are stubs. Optional `SIMFLAGS='--ram-segments 3'` supplies a model
mapper in slot 3. There is no VDP, audio or timing-accurate MSX model.

Static local selection still depends on the proven source call/activation graph.
Use zero `frame_ram_bytes` before editing generated modules or overriding them;
otherwise changes invalidate that proof and are rejected. C/assembly edits are
inspectable escape hatches whose source-semantic correctness requires review.
