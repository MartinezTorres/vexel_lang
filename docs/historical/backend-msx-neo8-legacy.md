> Historical frontend design. Use [the current backend contract](../backend-msx-neo8.md) for executable commands.

# MSX NEO-8: target contract, editable realization, and optimization

This defines a concrete experimental backend and distinguishes its implemented
path from the next solver mechanisms. The current exporter produces a complete
SDCC + megalinker project. It builds an MSX ROM, exports placement measurements,
and leaves C, assembly, startup, and Makefiles available for editing.

## Build and inspect

```sh
make -j4
build/vexel examples/neo8.vx --emit-msx build/my-neo8 --msx-entry entry
make -C build/my-neo8 -j4
make -C build/my-neo8 simulate SIMFLAGS='--irq-period 997 --expanded-slot'
```

Requirements for the generated project: SDCC, `sdasz80`, a host C++17 compiler,
Make, and Python 3. SDCC 4.2.0 is exercised by the integration tests. The C ABI
is explicitly `__sdcccall(0)`; do not inherit a compiler's default. `make test-msx`
requires the real toolchain and tests compilation, linking, section checks, and
replacing a generated module with assembly. Host differential tests separately
compare generated C with the Vexel resolver under all three graph schedulers.
An additional suite executes linked Z80 instructions on an independent CPU
core with a bounded NEO-8 model and BIOS stubs. It covers cross-bank callbacks,
both mapper-register bytes, IFF preservation, expanded slots, arithmetic,
startup initialization, traps, and observed stack-budget violations. It is
not full-system MSX emulation or hardware validation.

The project vendors the unmodified MIT megalinker at commit
`18eb466f02e6d14d3efef0fd02d2325b364b28b3`. No network access is required to build.
The generated Makefile can use another compatible `MEGALINKER` executable.

The independent relational implementation also exports banked projects through
`experimental/relational/project_msx.py`. Its closed C projection contains
separate source/function modules and resident mapper wrappers; the Makefile's
`BRIDGE_SOURCE` selects `vx_banked.c`, while `vx_bridges.c` remains usable for
host comparison. `make relational-math-msx` exercises combined integer,
fixed-point and floating-point helpers. `make relational-banks-msx` checks
generated, handwritten C and assembly implementations of the same module ABI.
See the [completion ledger](relational-completion.md) for its current limits
and executable evidence; it is not yet the consolidated implementation.

`make simulate` records workload observations in `build/execution.json`, bound
to the ROM, plan, placement and harness digests. It stops at the first HALT.
Its optional synthetic interrupt schedule and BIOS stubs are explicit model
inputs; observed stack use does not establish a worst-case bound. The pinned
zlib-licensed CPU core is included only for the host runner.

`make msx-experiment` in the repository runs a fixed two-candidate experiment:
automatic versus static temporaries for the same closed computation. It checks
eight inputs against the resolver and compares resident bytes, ROM section
bytes, observed stack, model ticks and mapper traffic. The first run reduced
observed stack from 93 to 75 bytes while increasing resident RAM from 2086 to
2250 bytes. [Execution evidence and the next solver abstraction](realization-evidence.md)
explains the measured tradeoff, its scope, and the proposed evidence model.

## Machine profile

The profile is named `msx-neo8-bios-16k`. Its baseline is a Z80 MSX with a BIOS
and RAM covering CPU page 3. It does not require MSX2, extra RAM, or a V9938.
It keeps the BIOS interrupt environment; direct VDP protocols are future
bindings with their own ordering requirements.

NEO-8 has six 8 KiB windows and two-byte segment registers. The v1.2 contract
allows 14 segment bits; the two highest bits are reserved. Its ROM begins with
the MSX header and `ROM_NEO8` at offset 16. Registers are written low byte at
the even address and high byte at the following address. The authoritative
table, rather than the older illustrative pseudocode's high-byte mask, defines
the v1.2 capacity. [NEO mapper specification](https://aoineko.org/msxgl/index.php?title=NEO_mapper)

| CPU range | NEO window / megalinker name | Register | Profile role |
|---|---|---|---|
| 0000–1FFF | 0 / Y | 5000 | BIOS selected; cartridge window unused |
| 2000–3FFF | 1 / Z | 5800 | BIOS selected; cartridge window unused |
| 4000–5FFF | 2 / A | 6000 | Startup/header, segment 0 remains mapped |
| 6000–7FFF | 3 / B | 6800 | Banked function modules and their constants |
| 8000–9FFF | 4 / C | 7000 | Initial segment 2; future data window |
| A000–BFFF | 5 / D | 7800 | Initial segment 3; future data window |
| C000–FFFF | CPU page 3 | — | Actual RAM; never select the cartridge here |

These mapping choices are a conservative implementation plan, not additional
hardware restrictions. The generated project permits changing the plan; its
checks cover the default profile and must evolve with a different runtime.

Runtime fixed point uses integer ABI storage with the source type's fraction.
Only projects containing residual rescaling, fixed multiplication or division
enable `FIXED_RUNTIME` in `generated/modules.mk`. The large arithmetic helpers
are inspectable in `runtime/vx_fixed.c` and placed in a banked B-window module.
Small resident wrappers save and restore the caller's mapping. Their two-limb
intermediates preserve all product/quotient bits before truncation and wrapping,
without requiring a native 64-bit integer or a floating-point library.

Runtime IEEE binary16 and binary32 also carry raw unsigned bits across C
interfaces. Residual floating arithmetic/conversions enable `FLOAT_RUNTIME` and
link `runtime/vx_float.c` into a B-window bank. Resident wrappers restore the
caller's mapping. The implementation uses integer limbs, nearest-even rounding,
gradual underflow, signed zeros and canonical arithmetic NaNs. Representation
copies preserve NaN payloads. Float-to-integer/fixed conversion traps with
reason 6 on a non-finite value, including a discarded conversion result.
Source binary64 remains available for resolved computations; residual binary64
requires another target realization. Generated prototypes document raw-bit ABI
types so handwritten C/assembly does not accidentally pass native C floats.

`--msx-rom-segments N` describes the *selected physical cartridge's* capacity,
not the maximum theoretically supported by the format. The default is 256
segments (2 MiB). The current linker produces a minimum 128 KiB image, so the
exporter accepts 16–16384 segments. It checks the resulting ROM size against
this limit. Startup and wrappers use 16-bit register writes, including segments
above 255. ROM bank numbers are unrelated to RAM-mapper segment numbers.

Startup obtains the cartridge slot from the existing page-1 selection, selects
it in CPU page 2 through the BIOS, initializes windows A–D, clears resident
storage, and copies `_HOME` plus initialized data. It requires BIOS HIMEM at
least F380 and otherwise halts before using the fixed stack contract.

## The 16 KiB RAM account

Persistent globals occupy resident storage and have explicit graph read/write
obligations. Uninitialized internal storage has an initialization flag; a read
before a whole-value assignment traps with reason 7. Non-constant initializers
are separate editable bank modules. A small resident guard runs each initializer
once on first access and restores the caller's bank. Reentry while initialization
is in progress traps with reason 8, including an external callback or interrupt
that attempts the same initialization. Initialization guards do not establish
general atomicity of subsequent accesses.

External `!name:Type;` storage uses a volatile C symbol `vx_ext_name` provided by
the project. Backend-bound `!!name:Type;` requires one `[[msx_address(N)]]` or
`[[msx_port(N)]]` annotation. Ports require `#u8`; addresses must fit their whole
physical value in the 16-bit CPU address space. These declarations are explicit
hardware contracts: the author supplies a usable address and manages any
required device protocol or bank selection. They do not allocate mapper RAM or
prove ownership of an arbitrary address. `plan.json` records space, address,
size and source type. Host-C builds provide separate shadow variables for these
bindings; this tests language behavior without pretending to emulate a device.

Generated accesses use volatile target storage and copy values at the specified
sequence points. No pointer type is added to Vexel. Addresses and C layout live
in the target realization, while module identity, types, effects and logical
receiver locations remain solver facts.

Default address intervals, with exclusive upper bounds:

| Interval | Bytes | Ownership |
|---|---:|---|
| C000–EF80 | 12160 | Resident routines, globals, static frames, remaining free space |
| EF80–F380 | 1024 | Reserved stack budget |
| F380–10000 | 3200 | BIOS/system work area reservation |

`--msx-stack N` changes the split below F380. The fixed system reservation is
part of this BIOS profile, not a claim that every MSX software environment has
exactly the same needs. DOS, a custom interrupt environment, or another stack
top requires another explicit contract.

Megalinker's actual section map supplies resident placement. The project
checker rejects overlap, placement beyond the stack boundary, invalid ROM
headers, page-0 cartridge allocations, and ROM-capacity violations. Invalid ROM
targets are removed by Make's `.DELETE_ON_ERROR`.

A reserved stack budget is **not a proven stack bound**. The report records
`stack_proof: unproven`. Counting C locals would miss SDCC spills, saved
registers, ABI argument traffic, arithmetic helpers, BIOS calls and interrupt
frames. This prototype makes no stack-depth safety claim. A later analyzer
must inspect generated assembly, account for calling contexts and interrupts,
and require contracts for handwritten code. Unbounded recursion must never be
given a fabricated finite estimate.

## Target facts arrive before source resolution

Selecting `--emit-msx` registers ordinary constant nodes before solving roots:

| Reserved name | Meaning |
|---|---|
| `__target_ram_bytes` | 16384 baseline physical bytes |
| `__target_rom_bank_bytes` | 8192 bytes per cartridge segment |
| `__target_rom_segments` | Configured physical ROM capacity |
| `__target_stack_bytes` | Selected reserve |
| `__target_resident_limit` | First address reserved for stack |
| `__target_ram_mapper_guaranteed_segments` | 0 in this baseline profile |

Their type is an exact integer. Source expressions depend on their graph node
IDs, so target facts can control array dimensions, specialization, constants
and demanded branches. They cannot be redefined or shadowed by source names.
These target-specific identifiers are experimental bindings, not new keywords.

Known target inputs, implementation decisions, and measured consequences are
kept distinct. `generated/plan.json` records the first two. After compilation
and linking, `build/resolution.json` records measurements for a particular ROM
SHA-256 and input fingerprints. Changing C, assembly, compiler options, or
placement requires measuring the new candidate. Old measurements are not
silently promoted to unconditional source constants.

The current bridge is deliberately limited: target constants participate in
the Vexel graph; the generated project's checker validates physical placement
after linking. Arbitrary linker facts do not yet feed back into a live Vexel
worklist. Independent fact components, candidate revisions, and general
bidirectional physical constraints remain architectural work.

## Logical operations and physical access

The C projector consumes resolved bindings, overload choices, types, values and
state edges. It does not repeat language analysis. It preserves evaluation
order with explicit temporaries and conditional blocks, including a constant
result that still performs an external effect. Pure cached invocation results
must not replay effects from the original caller's arguments.

Each exported function has a banked `vxi_NAME` implementation and a resident
`vx_NAME` wrapper. The wrapper saves B's segment, maps the implementation,
calls it, restores the previous segment, and returns. Consequently the code
performing the switch cannot unmap itself. A nested call through another
resident export wrapper restores its caller's mapping. A direct call to a
foreign `vxi_NAME` is outside the contract.

`&` invocations expand into the caller's graph. Demanded runtime `&~` invocations
become separate specialized C functions in the same module. Each module must
fit one 8 KiB segment including constant tables. Oversized modules fail;
automatic partitioning, ROM asset chunking, and bank-aware call clustering
are not yet implemented. This is one valid initial placement policy rather
than an optimal arrangement.

The projector supports scalar runtime integers of widths 1–32 and Booleans,
internal fixed arrays, array mutation and value copies, nested array shapes,
runtime loops, and unrolled iteration/repetition. Internal array storage is flat
within a generated C struct; explicit element-copy loops preserve Vexel value
semantics without introducing a source pointer. Shapes remain graph facts.
Materialized calls support aggregate arguments/results, captured locations and
indexed receiver designations. Public aggregate function signatures use named
records; generated C passes input aggregates by `const` address and returns into
caller-owned storage. Deep comparisons and portable byte/Boolean representation
casts are projected explicitly, independently of C padding. Public constants are
data symbols in resident `HOME` storage, including records, arrays and strings;
their size participates in the same memory checks. External
bindings use resident `vx_ext_NAME` functions with explicit prototypes and no
invented implementations. `msx_putchar(#u8)` is the supplied BIOS binding.

C arithmetic must preserve Vexel, including wrapping, arithmetic right shifts,
and minimum signed integer divided by -1. The supplied resident helpers use
unsigned arithmetic and explicitly check invalid shifts, zero division and
indices. Their baseline prioritizes correctness; measured alternatives can
later select smaller-width instructions or specialized assembly.

Possible traps and runtime loops retain a control effect even if their result
is constant. The projector must emit that control before returning the value.
It must also restore its graph-to-C memoization context at a loop boundary:
emitted loads inside a region execute again on every runtime iteration.

The target's internal C and assembly naturally use addresses. Vexel retains
logical values and resource operations; it does not acquire a universal source
pointer type. In particular, an eventual VRAM object is not interchangeable
with CPU RAM merely because both have numeric offsets.

## Per-function reentrancy and stack tradeoffs

The relational executable accepts `--frame-ram-bytes N` alongside its source
activation contract. Zero retains automatic locals. A positive budget permits
exclusive local buffers in the closed execution graph. Recursive functions
remain automatic, while an exclusive nonrecursive child can qualify. Open
boundary edges and possible activation overlap prevent sharing. The report's
`frame_planning` section records decisions and local-byte costs; ABI parameters
remain automatic and no whole-program stack bound is inferred. The MSX checker
measures total resident RAM and rejects code edits that invalidate this graph
proof. Resolve with a zero frame budget before overriding generated modules.
`make relational-frame-plan-msx` compares both policies on the same recursive
workload. The following explicit-exclusive option belongs to the established
executable.

Automatic C locals are the default. The backend does not use an 8051-style
SDCC overlay switch and assume the Z80 acquired the same behavior. It can
instead choose explicit static temporaries for a particular exported
activation and its materialized helpers:

```sh
build/vexel examples/neo8.vx --emit-msx build/neo8-static \
    --msx-exclusive sample
```

This option supplies a **caller contract**: activations of that export never
overlap. The solved region must also contain no external effects that could
hide callbacks. Functions in a recursive invocation component retain automatic
storage even when their enclosing exported entry uses this contract. The
resulting static storage consumes resident RAM and is measured after SDCC;
before-codegen temporary-byte counts are explicitly labeled as estimates.

The resident wrapper has a busy guard to diagnose synchronous misuse. It is
not an interrupt synchronization protocol. Custom ISRs/NMIs must not enter
generated banked exports or change mapper state in this first profile. BIOS
interrupts remain enabled outside the short bank-update sequence. The latter
preserves the caller's IFF and updates mapper state and its shadow together.

The language-level direction is to derive interference from graph sources,
sinks and their permitted activation overlap. Platform interrupts and threads
belong to the boundary adapter; the source solver should not reconstruct those
mechanisms. Two distinct mutable storage obligations may share bytes only when
their required lifetimes do not overlap. Internal calls and recursion also
contribute lifetime relationships. An external output alone does not establish
reentry. The current blanket external-effect rejection is a conservative
implementation restriction to replace with these contracts. See
[activation contracts](activation-contracts.md) for the proposed model and its
acceptance cases. Candidate choices belong to a plan revision; language facts
stay true.

## Dependent records and bundled math

Dependent record dimensions retain runtime checks on construction, updates and
representation decoding; a failed check traps with reason 9. Runtime inputs to
an invariant are owned record fields. Compile-time lexical schema parameters
are frozen in the graph and cannot become references into an expired stack frame.

Bundled math uses a separate optional bank module, `runtime/vx_math.c`, with a
resident `vx_math` wrapper that saves and restores window B. It implements
binary16/binary32 roots and rounding, classification, min/max and copy-sign,
fixed-point rounding, and integer square roots without host floating arithmetic
or an SDCC standard library dependency. A negative integer square root traps
with reason 10, including when its result is discarded. `pow` projects repeated
squaring using the existing integer, fixed or floating multiplication rules.
Unused math helpers are not linked. All generated files remain inspectable and
subject to the existing source fingerprints and editable-project rules.

## Optional RAM mapper: defined extension contract

Additional RAM is an independent storage provider, not a larger flat pointer
space and not an automatic enlargement of the resident stack. The current
runtime does not discover or allocate mapper RAM. Its guaranteed capacity is
zero; a capability discovered at startup is a runtime input.

The proposed provider interface has these obligations:

1. Obtain ownership of segments from the execution environment. In an OS
   environment use its allocation service; in a dedicated cartridge profile
   accept an explicit board/application ownership contract. A responding mapper
   port or reported capacity is not evidence that a segment is free.
2. Return a logical handle describing provider, mapper slot, owned segment and
   extent. The application may have an optional/failure path when acquisition
   fails. This is explicit platform resource management, not a language heap.
3. Borrow CPU page 2 for a bounded operation while code remains in page 1 or
   resident RAM and the stack remains in page 3. Mapping an ordinary 16 KiB RAM
   segment there temporarily displaces both NEO windows C and D.
4. Establish and restore slot/subslot selection and mapper-register state,
   accounting for registers that affect multiple mapper devices. Never assume
   a universal slot, readback behavior, segment ownership, or OS policy.
5. Return with the cartridge mapping restored, invalidate affected access-cache
   facts, and forbid references into the borrowed window from escaping.
6. Specify interrupt access to the borrowed page. Preserve IFF; use a bounded
   critical section or a proven ISR-compatible mapping protocol.

An operation can therefore be described as `read(handle, offset)` or a bounded
transfer, without making `handle + offset` a CPU pointer. Physical placement and
access protocols are facts attached to a realization of the logical object.
If optional RAM is absent, the same program can select a smaller cache or a
streaming strategy. Its *minimum* working configuration still fits page 3.

## Handwritten assembly and continued solving

The exported project is a maintained artifact, not an opaque temporary folder.
Create `user/vxp_NAME.s` or `.c` to replace one module; Make selects it in place
of the generated version. Preserve its module name, `_CODE` placement, symbol
and ABI. Other user modules default to resident `_HOME` code. Generated SDCC
assembly, section maps and symbol maps remain available for inspection.
`generated/node-map.json` connects C temporary/slot IDs to Vexel source sites.

The exporter refuses nonempty destinations. Re-export elsewhere and diff/merge
changes. Export fingerprints detect edited generated files; linked reports
record actual inputs and handwritten overrides. These fingerprints detect
edits, not adversarial tampering. Static-frame assumptions are invalidated by
overrides to the affected module or edits to shared runtime/ABI code. Ordinary
user code still carries the application's explicit execution-context contract.

For deeper integration, an assembly replacement should provide an interface
contract: argument/result ABI, registers clobbered, stack bound, effects,
mapping requirements, interrupt behavior, and any cycle bound. An unknown field
remains unknown and blocks only optimizations that need it. It must not be
interpreted as zero stack or no effects.

## Optimization proposals, including an LLM

There need not be a globally optimal answer. First find a legal implementation,
then explore candidates under a time or build budget. Useful proposal knobs
include module grouping, inlining/materialization, static-frame eligibility,
constant representation, asset chunking, hot code placement, calling convention
adapters, and assembly replacements. SDCC and megalinker are measurement tools
inside that exploration.

An LLM could propose a candidate and explain its hypothesis. Its output would
not establish a fact. A proposal should contain the base input hash, scoped
edits or policy changes, required assumptions, predicted benefit and validation
requirements. A deterministic controller builds candidates in isolated
directories, checks their contracts, compares behavior, and measures actual
results. It retains only candidates satisfying correctness and resource limits.

Cost is a vector: resident bytes, maximum stack if proved, ROM bytes, execution
time for specified workloads, and bank-switch traffic. Optimizing one can worsen
another. Reports must distinguish measured quantities, proven bounds, estimates
and unknowns. Stable tie-breaking and explicit seeds make repeated exploration
reviewable. A failed candidate does not invalidate the last successful build.

No LLM service or autonomous optimization loop is connected in this prototype.
The editable project and candidate/measurement distinction provide a place to
add one without giving a heuristic authority over language semantics.
The proposed [search provider](realization-evidence.md#optional-llm-search-provider)
also supports guidance during joint resolution, scoped backtracking after late
constraints, and recorded proposals for replay without a live model.

## Sources and implementation boundaries

- [NEO mapper specification](https://aoineko.org/msxgl/index.php?title=NEO_mapper)
  defines the mapper register and ROM identification contract.
- [Pinned megalinker source](https://github.com/MartinezTorres/megalinker/tree/18eb466f02e6d14d3efef0fd02d2325b364b28b3)
  supplies the object format, placement API and linker behavior; its NEO startup
  is the basis of the adapted, licensed runtime.
- [SDCC manual](https://sdcc.sourceforge.net/doc/sdccman.pdf) defines Z80 calling
  conventions and compiler integration. Actual generated assembly is part of
  the validation evidence for the installed compiler version.

Next structural work is independently demandable fact properties and scoped
candidate plans, followed by an owned RAM provider and instruction-level stack
analysis. A textual intermediate Vexel format is useful for inspection, but it
is not a prerequisite for this target or a barrier to target facts entering
source resolution.
