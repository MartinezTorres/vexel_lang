#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"

ANALYSIS_DIR="$ROOT/frontend/src/analysis"
TC_HEADER="$ROOT/frontend/src/type/typechecker.h"
OPT_HEADER="$ROOT/frontend/src/transform/optimizer.h"
COMPILER_HEADER="$ROOT/frontend/src/cli/compiler.h"

if rg -q '#include "evaluator.h"' "$ANALYSIS_DIR"; then
  echo "Analysis must not depend on evaluator ownership headers" >&2
  exit 1
fi

if rg -q 'CompileTimeEvaluator' "$ANALYSIS_DIR"; then
  echo "Analysis must consume optimizer facts, not instantiate evaluator" >&2
  exit 1
fi

if ! rg -q '#include "cte_value.h"' "$TC_HEADER"; then
  echo "TypeChecker public header must expose CT value types via core/cte_value.h" >&2
  exit 1
fi

if rg -q '#include "evaluator.h"' "$TC_HEADER"; then
  echo "TypeChecker public header must not leak evaluator ownership" >&2
  exit 1
fi

if ! rg -q '#include "cte_value.h"' "$OPT_HEADER"; then
  echo "Optimizer public header must use core/cte_value.h" >&2
  exit 1
fi

if rg -q '#include "evaluator.h"' "$OPT_HEADER"; then
  echo "Optimizer public header must not leak evaluator ownership" >&2
  exit 1
fi

if rg -q '#include "ast.h"|#include "typechecker.h"' "$COMPILER_HEADER"; then
  echo "Compiler public header must stay narrow and avoid semantic header leakage" >&2
  exit 1
fi

echo "ok"
