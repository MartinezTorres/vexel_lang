#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TC_CPP="$ROOT/frontend/src/type/typechecker.cpp"
TC_HEADER="$ROOT/frontend/src/type/typechecker.h"

if rg -q '#include "evaluator.h"' "$TC_CPP" "$TC_HEADER"; then
  echo "TypeChecker must not directly own the compile-time evaluator engine" >&2
  exit 1
fi

if rg -q 'CompileTimeEvaluator\s+evaluator\s*\(' "$TC_CPP"; then
  echo "TypeChecker must not construct ad-hoc compile-time evaluators" >&2
  exit 1
fi

echo "ok"
