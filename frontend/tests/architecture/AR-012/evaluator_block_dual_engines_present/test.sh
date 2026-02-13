#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TARGET="$ROOT/frontend/src/transform/evaluator.cpp"

if ! rg -q "success = eval_block_vm\\(expr, result, handled\\);" "$TARGET"; then
  echo "expected eval_block_vm dispatch in unresolved dual-engine path" >&2
  exit 1
fi

if ! rg -q "success = eval_block_fallback\\(expr, result\\);" "$TARGET"; then
  echo "expected eval_block_fallback dispatch in unresolved dual-engine path" >&2
  exit 1
fi

if ! rg -q "bool CompileTimeEvaluator::eval_block_fallback\\(ExprPtr expr, CTValue& result\\)" "$TARGET"; then
  echo "expected eval_block_fallback implementation in unresolved dual-engine path" >&2
  exit 1
fi

echo "ok"
