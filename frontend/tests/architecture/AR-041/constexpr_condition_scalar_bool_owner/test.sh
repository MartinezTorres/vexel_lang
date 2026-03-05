#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
TARGET="$ROOT/frontend/src/type/typechecker_expr_control.cpp"
FN="$(awk '
  /std::optional<bool> TypeChecker::constexpr_condition\(ExprPtr expr\) \{/ { in_fn=1 }
  in_fn { print }
  in_fn && /^}/ { exit }
' "$TARGET")"

if [[ -z "$FN" ]]; then
  echo "missing constexpr_condition implementation in typechecker_expr_control.cpp" >&2
  exit 1
fi

if ! rg -q "return cte_scalar_to_bool\(value\);" <<<"$FN"; then
  echo "constexpr_condition must use the shared scalar-to-bool conversion" >&2
  exit 1
fi

if rg -q "std::holds_alternative<CTExactInt>" <<<"$FN"; then
  echo "constexpr_condition should not duplicate scalar-to-bool variant handling" >&2
  exit 1
fi

echo "ok"
