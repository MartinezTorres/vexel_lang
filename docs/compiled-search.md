# Compiling part of resolution

`--compiled-search DIRECTORY` projects a closed finite relation set into a
native C++ filter. The solver creates a fresh subdirectory with `relations.cpp`,
a Makefile, the resulting shared library and a build log. It invokes Make
without a shell command string and loads the successfully built library.

```sh
build/vexel examples/sudoku.vx --root solution \
    --compiled-search build/search-kernels --verify-search-kernel \
    --search-report build/sudoku-search.json
```

The projection contains allowed tuples of domain indices. Source numeric
operations have already established these tuples using Vexel's own exact,
wrapping, conversion and guarded-trap rules. Native C++ never reinterprets a
source arithmetic operation. It only asks which allowed rows remain compatible
with the current active domains and marks their supported values.

The routine is a pure operation over caller-supplied active-domain masks. It
returns supported masks, a tuple-check count, and one of support, contradiction,
or exhausted budget. The ordinary solver retains the agenda, trail, decisions,
conflicts, rollback, observable-result uniqueness and optional advice protocol.
Thus native execution accelerates one repeated question without becoming a
second authority for the language's meaning.

`--verify-search-kernel` runs the ordinary filter beside every native call and
compares the status, check count and complete support masks, including partial
work on exhaustion. A disagreement is an internal error, never a source
contradiction. Tests compare ordinary and native resolution for satisfiable,
ambiguous and impossible systems, actual backtracking, budgets and Sudoku.

A fresh directory prevents stale binaries from a different relation revision
from being reused. Generated files are inspectable and can be rebuilt with
Make; a subsequent Vexel run creates its own fresh kernel. Compilation is bounded
to 30 seconds and 64 KiB of diagnostics. Failed compilation or loading is
recorded in the search report and uses ordinary filtering. The ordinary solver
still works without selecting this option or installing an additional compiler.
The current host implementation uses POSIX processes and dynamic libraries.

Compilation costs time. Small problems, or problems dominated by source graph
construction, may be faster without it. This first implementation does not
choose a compilation threshold automatically, persist a cross-run kernel cache,
or compile arbitrary source functions on the host. Those are separate policies
that need cost and context information. It establishes the execution interface
and an experimentally checkable interchangeability contract first.
