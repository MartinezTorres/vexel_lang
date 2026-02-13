#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TARGET="$ROOT/frontend/src/analysis/analysis_reentrancy.cpp"

if rg -q "return \\(fallback == 'R' \\|\\| fallback == 'N'\\) \\? fallback : 'N';" "$TARGET"; then
  echo "reentrancy defaults must hard-fail invalid values, not normalize to N" >&2
  exit 1
fi

echo "ok"
