#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TARGET="$ROOT/frontend/src/transform/optimizer.cpp"

if rg -q 'CompileTimeEvaluator\s+evaluator\s*\(' "$TARGET"; then
  echo "Optimizer must use a canonical CTE execution service, not ad-hoc evaluator construction" >&2
  exit 1
fi

echo "ok"
