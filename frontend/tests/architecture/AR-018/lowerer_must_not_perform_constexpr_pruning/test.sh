#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
LOWERER_CPP="$ROOT/frontend/src/transform/lowerer.cpp"

if rg -q "constexpr_condition\\(" "$LOWERER_CPP"; then
  echo "lowerer must stay shape-only; constexpr pruning belongs to optimizer/residualizer" >&2
  exit 1
fi

echo "ok"
