#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TARGET="$ROOT/frontend/src/analysis/analysis_reentrancy.cpp"

if ! rg -q "auto normalize_ctx" "$TARGET"; then
  echo "missing normalize_ctx helper; audit expectation changed" >&2
  exit 1
fi

if ! rg -q "return \\(fallback == 'R' \\|\\| fallback == 'N'\\) \\? fallback : 'N';" "$TARGET"; then
  echo "expected unresolved behavior: invalid defaults are no longer normalized to N" >&2
  exit 1
fi

echo "ok"
