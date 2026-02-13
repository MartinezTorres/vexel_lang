#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TARGET="$ROOT/frontend/src/transform/evaluator.cpp"

if rg -q "Fallback for unresolved named types: keep the known fields as-is" "$TARGET"; then
  echo "compile-time coercion must not have unresolved named-type fallback behavior" >&2
  exit 1
fi

if rg -q "out_comp->fields\\[entry.first\\] = clone_value\\(entry.second\\);" "$TARGET"; then
  echo "named-type coercion must not keep fields without resolved type contract" >&2
  exit 1
fi

echo "ok"
