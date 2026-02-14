#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OPT_CPP="$ROOT/frontend/src/transform/optimizer.cpp"
CTE_ENGINE_HEADER="$ROOT/frontend/src/transform/cte_engine.h"

if ! rg -q "kMaxCteFixpointIterations" "$OPT_CPP"; then
  echo "optimizer must use an explicit CTE fixpoint scheduler" >&2
  exit 1
fi

if ! rg -q "cte_engine_\\.query\\(" "$OPT_CPP"; then
  echo "optimizer must consume CTE through the canonical CTE engine service" >&2
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

if rg -q "CompileTimeEvaluator\\s+evaluator\\s*\\(" "$OPT_CPP"; then
  echo "optimizer must not instantiate ad-hoc evaluators" >&2
  exit 1
fi

if ! rg -q "class CTEEngine" "$CTE_ENGINE_HEADER"; then
  echo "frontend must provide a dedicated CTE engine abstraction" >&2
  exit 1
fi

echo "ok"
