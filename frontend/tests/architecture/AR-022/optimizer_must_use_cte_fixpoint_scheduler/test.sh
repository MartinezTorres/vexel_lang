#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OPT_CPP="$ROOT/frontend/src/transform/optimizer.cpp"
EVAL_HEADER="$ROOT/frontend/src/transform/evaluator.h"

if ! rg -q "kMaxCteFixpointIterations" "$OPT_CPP"; then
  echo "optimizer must use an explicit CTE fixpoint scheduler" >&2
  exit 1
fi

if ! rg -q "set_value_observer" "$OPT_CPP"; then
  echo "optimizer must consume evaluator traces through set_value_observer" >&2
  exit 1
fi

if rg -q "void Optimizer::visit_expr\(" "$OPT_CPP"; then
  echo "legacy recursive per-node optimizer visitor must not be the primary CTE owner" >&2
  exit 1
fi

if rg -q "void Optimizer::visit_stmt\(" "$OPT_CPP"; then
  echo "legacy recursive per-statement optimizer visitor must not be the primary CTE owner" >&2
  exit 1
fi

if ! rg -q "ExprValueObserver" "$EVAL_HEADER"; then
  echo "evaluator must expose expression-value observer hook for traced CTE" >&2
  exit 1
fi

echo "ok"
