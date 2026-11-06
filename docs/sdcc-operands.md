# Preserving shared operands in the SDCC projection

The relational schema oracle exposed incorrect byte arithmetic in the locally
available SDCC 4.2.0 #13081, 4.5.0 #15242 and 4.6.0 #16555 Z80 compilers. This
minimal C example reproduces the assembly error with `-mz80 --sdcccall 0
--opt-code-size`:

```c
#include <stdint.h>
void save(uint8_t, uint8_t, uint8_t) __sdcccall(0);
void example(uint8_t x) __sdcccall(0) {
    save(x, ((uint32_t)x + 1UL) & 255UL,
            ((uint32_t)x + 2UL) & 255UL);
}
```

The generated code loads `x` into A, adds two in A, and then reuses that modified
register for both the first argument and the addition of one. With zero input,
the arguments become `(2,3,2)` instead of `(0,1,2)`. Disabling peepholes, GCSE,
LOS PRE, or changing frame-pointer and allocator settings did not remove the
observed failure. This is a local reproducer, not a claim that all arithmetic
in these compiler releases is incorrect.

The relational C projection conservatively keeps residual input parameters in
volatile automatic storage under SDCC. Reads of call-result and loop snapshots
also use volatile lvalues of their actual storage type. The host projection
retains ordinary reads. The result buffers' pointer ABI stays unchanged.
These accesses implement stable Vexel values; they do not introduce external
effects, shared scratch storage, or new language semantics.

This costs loads and stack space. A future realization may remove individual
barriers after certifying the selected toolchain and expression family. A
version number alone does not justify removing them. Generated C, assembly,
compiler flags and execution evidence remain inspectable in each project.

`make relational-schema-msx` exercises the original failing aggregate store
over every byte input, including wraparound, and compares host and Z80 results.
It also checks that invalid schema writes and constructors reach trap 19.
See the completion ledger for the latest fingerprinted run.
