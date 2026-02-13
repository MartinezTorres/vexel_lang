#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
OPT_HEADER="$ROOT/frontend/src/transform/optimizer.h"

if rg -q "const Expr\\*[^\\n]*constexpr_values" "$OPT_HEADER"; then
  echo "optimizer constexpr_values must be keyed by (instance, expr), not raw Expr*" >&2
  exit 1
fi

if rg -q "const Expr\\*[^\\n]*constexpr_conditions" "$OPT_HEADER"; then
  echo "optimizer constexpr_conditions must be keyed by (instance, expr), not raw Expr*" >&2
  exit 1
fi

echo "ok"
