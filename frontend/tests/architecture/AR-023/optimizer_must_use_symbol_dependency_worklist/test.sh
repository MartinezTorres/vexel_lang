#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OPT_CPP="$ROOT/frontend/src/transform/optimizer.cpp"

if ! rg -q "cte_engine_\\.query\\(" "$OPT_CPP"; then
  echo "optimizer must consume CTE through the canonical CTE engine query path" >&2
  exit 1
fi

if ! rg -q "std::queue<size_t> expr_queue_" "$OPT_CPP"; then
  echo "optimizer must keep an expression worklist queue" >&2
  exit 1
fi

if ! rg -q "std::queue<size_t> root_queue_" "$OPT_CPP"; then
  echo "optimizer must keep a root worklist queue" >&2
  exit 1
fi

if ! rg -q "enqueue_dependents\\(" "$OPT_CPP"; then
  echo "optimizer must requeue dependent work on symbol changes" >&2
  exit 1
fi

if rg -q "void run_context_roots\\(" "$OPT_CPP"; then
  echo "legacy full-rescan context-root pass must not be the primary owner" >&2
  exit 1
fi

if rg -q "void run_per_expr_queries\\(" "$OPT_CPP"; then
  echo "legacy full-rescan per-expression pass must not be the primary owner" >&2
  exit 1
fi

echo "ok"
