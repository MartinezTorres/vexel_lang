# Editable Vexel NEO-8 project

The whole program is resolved together, then emitted as independent bank
allocations. Each allocation's machine code and constants must fit 8192 bytes;
the size of its C text is not that measurement. Generated `bank_modules` records
identify the original `partition_owner`, its `partition_role` and effective
`chunk_operations`. Initial realization uses these facts to retry only rejected
partitions. `realization.json` records the decisions and measured failures;
its `realization_preferences` also records a rejected owner's compact division
choice when unrolled helpers exceed the bank or compiler resource budget.
Ordinary rebuilds preserve your project edits.

Run `make` with SDCC, sdasz80, Python 3 and a C++ compiler. No network or Vexel
executable is needed. The result is `build/game.rom`, SDCC assembly/listings,
megalinker maps and `build/resolution.json`. `local.mk` controls local toolchain
settings. The report fingerprints the ROM, inputs and objects.

C compilations also write Make dependency files using SDCC's preprocessor.
Incremental builds follow transitive includes, including nested headers in
`user/` and individual generated support components. Unrelated header edits do
not rebuild every module. A dependency-only preprocessor pass supplies empty
rules for removed headers (SDCC 4.2 omits them in its side-effect dependency
mode), and a missing dependency file forces its C object to be rebuilt.

SDCC runs are serialized across exported projects for the current user. The
default guard limits each invocation to 2 GiB of address space and 180 seconds,
and records peak RSS and status in `build/sdcc-resources.jsonl`. Explicit
`SDCC_MEMORY_MB` and `SDCC_TIMEOUT` settings belong in `local.mk`; build failure
does not automatically increase them. Smaller generated chunks are usually the
appropriate response to compiler resource exhaustion.
Default `SDCCFLAGS` also cap register-allocation search at 2,000 attempts per
node. Replacing those flags is a toolchain choice requiring fresh validation.

`generated/program-image.json` inventories the closed projection and its code
fragments. Packaging checks these products before export. Use `user/` overrides
for subsequent hand edits and validate their behavior against the source.

Implement your adapter in `user/main.c`. `generated/vexel.h` describes the ABI;
`generated/source-plan.json` records source roots, flattened parameters, result
leaves, configuration, ownership, access contracts and frame choices.

For finite occurrences, invoke `vx_NAME(...)`, then read scalar leaves through
`vx_NAME_read(index)` while following the reported begin/produce/release graph.
For streams, use `vx_NAME_submit(...)`, `vx_NAME_read_lease(token,index)` and
`vx_NAME_release(token)`. Submission returns zero on backpressure. Release
invalidates the token; it is never silently reused. The raw entry/read functions
are internal scratch operations in stream mode. Adapters serialize pool API
access; this is not an atomic multithread interface.

Required sinks are `vx_external_NAME(uint32_t *result, ...value leaves...)`.
Their result buffer belongs to the caller. Generated wrappers validate returned
ranges and schemas. Keep declared footprints and no-feedback promises. Target
pointers implement the ABI and do not become Vexel pointers.

## Edit a bank module

The project builds `generated/vx_banked.c` and the `BANK_MODULES` listed in
`generated/modules.mk`. `vx_bridges.c` is the unbanked reference projection.
To replace one module, add `user/vxp_NAME.c` or `user/vxp_NAME.s`; assembly wins.
Keep its `vxi_*` signature, SDCC Z80 ABI 0, and return/register behavior. Assembly
uses its matching `.module` name and `_CODE`. Call another bank through its
resident `vx_*` wrapper, which saves and restores window B.

Large functions can have continuation modules and `vxf_*.h` frame descriptions.
Their program counter and carried state preserve branches and loops across bank
cuts. Do not change the frame layout in one module without changing all users.
Each linked bank module must fit 8 KiB. Other user C modules reside in `_HOME`;
user assembly chooses `_HOME` explicitly. `LIBS` can add required SDCC libraries
for handwritten C. Generated arithmetic uses the provided runtime helpers.

Automatic local buffers permit module overrides. Nonzero static-local selection
depends on a closed call/activation graph: overrides or edits to that graph fail
the project check. Re-export with `--frame-ram-bytes 0` before those edits. Export never overwrites a
nonempty project, and rebuilding this project never regenerates your code.

## Memory and proof contracts

The resident profile needs RAM at 0xC000..0xFFFF and BIOS HIMEM >= 0xF380.
Page 0 remains BIOS; startup selects the cartridge in page 2, clears allocated
RAM and copies resident code. Stack top is 0xF380. Linked resident code, data,
stream bookkeeping and static locals must fit below the reserved stack.

Mapper storage is separate. When `MAPPER_SEGMENTS` is nonzero, the adapter calls
`vx_mapper_init(slot,first_segment,count)` once before accessing mapped globals.
It grants exclusive ownership of consecutive physical segments, excluding the
resident/BIOS segments. The target does not probe or allocate operating-system
RAM for you. Accessors borrow page 2, then restore the cartridge slot and caller
interrupt state. The adapter owns the mapper page-2 selection register. Extra
segments do not enlarge the resident stack.

`user/stack-contracts.json` contains `environment_bytes` and a `functions` map
from assembly symbols to additional stack-byte bounds. Supply actual BIOS and
external-adapter contracts, including any concurrent boundary workload. These
are recorded assumptions. The assembly analysis covers each source entry;
unknown control/SP behavior or unbounded recursion leaves it unproven. A nonzero
`call_depth` instruments bank wrappers with a checked depth bound. Editing the
guards/trap invalidates that assumption. A proved bound exceeding the reservation
fails the build; `stack_proof=1` also rejects an unfinished proof.

Fatal `vx_trap` records `vx_fault`, disables interrupts and halts. Generated
validation uses 16 for scalar/index checks, 17 for uninitialized storage, 18 for
initializer cycles, 19 for schema checks, 21 for stale leases, 22 for mapper
grants/access, 23 for the configured call-depth limit, and 24 for invalid bank
continuation state. A trap does not release leases or resume an activation.

## Run the bounded Z80 model

`make simulate` runs the linked ROM until its first HALT. The default main is an
empty adapter; a workload should set/check `vx_last_result`. Options include
`SIMFLAGS='--max-ticks 100000000 --expanded-slot --irq-period 997'` and
`--ram-segments N` for an optional model mapper in slot 3. With the latter, an
adapter can grant segments 1..N-1 while retaining segment 0 for resident use.

The model provides real Z80 execution, NEO-8 windows and 16 KiB resident RAM
filled with 0xA5. BIOS calls are stubs. It has no VDP/audio or MSX wait states.
Its optional synthetic ISR is not a real BIOS stack contract. Execution reports
contain outputs, fetched segments, mapper activity, hashes and observed stack
use. Those observations never replace the separate assembly bound. Runtime
traps, tick exhaustion and observed stack overflow fail simulation.

The full current language/target contract is in `docs/backend-msx-neo8.md`.
