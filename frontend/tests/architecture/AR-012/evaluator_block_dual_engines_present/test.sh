#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TARGET="$ROOT/frontend/src/transform/evaluator.cpp"

if rg -q "eval_block_fallback" "$TARGET"; then
  echo "compile-time block evaluation must use a single engine (no fallback interpreter path)" >&2
  exit 1
fi

echo "ok"
