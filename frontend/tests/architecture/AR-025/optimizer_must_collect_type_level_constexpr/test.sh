#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OPT_CPP="$ROOT/frontend/src/transform/optimizer.cpp"

if ! rg -q "void collect_type\\(const TypePtr& type, int instance_id\\)" "$OPT_CPP"; then
  echo "optimizer must own type-level constexpr expression collection" >&2
  exit 1
fi

if ! rg -q "collect_expr\\(type->array_size, instance_id, false\\)" "$OPT_CPP"; then
  echo "optimizer must collect array-size expressions from type nodes" >&2
  exit 1
fi

if ! rg -q "collect_type\\(stmt->var_type, instance_id\\)" "$OPT_CPP"; then
  echo "optimizer must collect variable type expressions" >&2
  exit 1
fi

if ! rg -q "collect_type\\(expr->target_type, instance_id\\)" "$OPT_CPP"; then
  echo "optimizer must collect cast target type expressions" >&2
  exit 1
fi

echo "ok"
